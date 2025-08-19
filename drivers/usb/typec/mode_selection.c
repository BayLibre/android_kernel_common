// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC.
 */

#include <linux/usb/typec_altmode.h>
#include <linux/slab.h>
#include <linux/usb/pd_vdo.h>
#include <linux/kfifo.h>
#include <linux/list.h>
#include "mode_selection.h"
#include "class.h"

/* Timeout for a mode entry attempt, ms */
static const unsigned int mode_selection_timeout = 4000;
/* Delay between mode entry/exit attempts, ms */
static const unsigned int mode_selection_delay = 1000;
/* Maximum retries for mode entry on busy status */
static const unsigned int mode_entry_attempts = 4;

static const char * const mode_names[TYPEC_ALTMODE_MAX] = {
	[TYPEC_ALTMODE_DP] = "DisplayPort",
	[TYPEC_ALTMODE_TBT] = "Thunderbolt3",
	[TYPEC_ALTMODE_USB4] = "USB4",
};

static inline enum typec_mode_type typec_svid_to_altmode(const u16 svid)
{
	switch (svid) {
	case USB_TYPEC_DP_SID:
		return TYPEC_ALTMODE_DP;
	case USB_TYPEC_TBT_SID:
		return TYPEC_ALTMODE_TBT;
	case USB_TYPEC_USB4_SID:
		return TYPEC_ALTMODE_USB4;
	}
	return TYPEC_ALTMODE_MAX;
}

/**
 * enum ms_state - Specific mode selection states
 * @MS_STATE_IDLE: The mode entry process has not started
 * @MS_STATE_INPROGRESS: The mode entry process is currently underway
 * @MS_STATE_ACTIVE: The mode has been successfully entered
 * @MS_STATE_CABLE_FAILED: The connected cable doesn't support the mode
 * @MS_STATE_TIMEOUT: Mode entry failed due to a timeout
 * @MS_STATE_FAILED: The mode driver reported the error
 */
enum ms_state {
	MS_STATE_IDLE = 0,
	MS_STATE_INPROGRESS,
	MS_STATE_ACTIVE,
	MS_STATE_CABLE_FAILED,
	MS_STATE_TIMEOUT,
	MS_STATE_FAILED,
};

/**
 * struct mode_selection_state - State tracking for a specific Type-C mode
 * @mode: The type of mode this instance represents
 * @priority: The mode priority. Lower values indicate a more preferred mode.
 * @list: List head to link this mode state into a prioritized list.
 * @enter: Flag indicating if the driver is currently attempting to enter or
 * exit the mode
 * @attempt_count: Number of times the driver has attempted to enter the mode
 * @state: The current mode selection state
 * @error: The outcome of the last attempt to enter the mode
 */
struct mode_selection_state {
	enum typec_mode_type mode;
	unsigned int priority;
	struct list_head list;

	bool enter;
	int attempt_count;
	enum ms_state state;
	int error;
};

/* -------------------------------------------------------------------------- */
/* port 'mode_priorities' attribute */

int typec_mode_set_priority(struct typec_altmode *adev,
	const unsigned int priority)
{
	struct typec_port *port = to_typec_port(adev->dev.parent);
	const enum typec_mode_type mode = typec_svid_to_altmode(adev->svid);
	struct mode_selection_state *ms_target = NULL;
	struct mode_selection_state *ms, *tmp;

	if (mode >= TYPEC_ALTMODE_MAX || !mode_names[mode])
		return -EOPNOTSUPP;

	list_for_each_entry_safe(ms, tmp, &port->mode_list, list) {
		if (ms->mode == mode) {
			ms_target = ms;
			list_del(&ms->list);
			break;
		}
	}

	if (!ms_target) {
		ms_target = kzalloc(sizeof(struct mode_selection_state), GFP_KERNEL);
		if (!ms_target)
			return -ENOMEM;
		ms_target->mode = mode;
		INIT_LIST_HEAD(&ms_target->list);
	}

	ms_target->priority = priority;

	while (ms_target) {
		struct mode_selection_state *ms_peer = NULL;

		list_for_each_entry(ms, &port->mode_list, list)
			if (ms->priority >= ms_target->priority) {
				if (ms->priority == ms_target->priority)
					ms_peer = ms;
				break;
			}

		list_add_tail(&ms_target->list, &ms->list);
		ms_target = ms_peer;
		if (ms_target) {
			ms_target->priority++;
			list_del(&ms_target->list);
		}
	}

	return 0;
}

int typec_mode_get_priority(struct typec_altmode *adev, unsigned int *priority)
{
	struct typec_port *port = to_typec_port(adev->dev.parent);
	const enum typec_mode_type mode = typec_svid_to_altmode(adev->svid);
	struct mode_selection_state *ms;

	list_for_each_entry(ms, &port->mode_list, list)
		if (ms->mode == mode) {
			*priority = ms->priority;
			return 0;
		}

	return -EOPNOTSUPP;
}

void typec_mode_selection_destroy(struct typec_port *port)
{
	struct mode_selection_state *ms, *tmp;

	list_for_each_entry_safe(ms, tmp, &port->mode_list, list) {
		list_del(&ms->list);
		kfree(ms);
	}
}

/* -------------------------------------------------------------------------- */
/* partner 'mod_selection' attribute */

/**
 * mode_selection_next() - Process mode selection results and schedule next
 * action
 * @partner: pointer to the partner structure
 * @ms: pointer to active mode_selection_state object that is on top in
 * mode_sequence FIFO.
 *
 * The mutex protecting the mode_sequence FIFO must be held by the caller
 * when invoking this function.
 *
 * This function evaluates the outcome of the previous mode entry or exit
 * attempt. Based on this result, it determines the next mode to process and
 * schedules `mode_selection_work()` if further actions are required.
 *
 * If the previous mode entry was successful, the mode selection sequence is
 * considered complete for the current cycle.
 *
 * If the previous mode entry failed, this function schedules
 * `mode_selection_work()` to attempt exiting the mode that was partially
 * activated but not fully entered.
 *
 * If the previous operation was an exit (after a failed entry attempt),
 * `mode_selection_next()` then advances the internal list of candidate
 * modes to determine the next mode to enter.
 */
static void mode_selection_next(
	struct typec_partner *partner, struct mode_selection_state *ms)
	__must_hold(&partner->mode_sequence_lock)
{
	if (!ms->enter) {
		kfifo_skip(&partner->mode_sequence);
	} else if (ms->state == MS_STATE_INPROGRESS && !ms->error) {
		ms->state = MS_STATE_ACTIVE;
		partner->active_mode = ms;
		kfifo_reset(&partner->mode_sequence);
	} else {
		if (ms->error) {
			ms->state = MS_STATE_FAILED;
			dev_dbg(&partner->dev, "%s: entry mode error %pe\n",
				mode_names[ms->mode], ERR_PTR(ms->error));
		}
		if (ms->error != -EBUSY || ms->attempt_count >= mode_entry_attempts)
			ms->enter = false;
	}

	if (!kfifo_is_empty(&partner->mode_sequence))
		schedule_delayed_work(&partner->mode_selection_work,
			msecs_to_jiffies(mode_selection_delay));
}

void mode_selection_complete(struct typec_altmode *alt, const int error)
{
	struct typec_partner *partner = to_typec_partner(alt->dev.parent);
	const enum typec_mode_type mode = typec_svid_to_altmode(alt->svid);
	struct mode_selection_state *ms;

	mutex_lock(&partner->mode_sequence_lock);
	if (kfifo_peek(&partner->mode_sequence, &ms)) {
		if (ms->mode == mode && ms->state == MS_STATE_INPROGRESS) {
			ms->error = error;
			cancel_delayed_work(&partner->mode_selection_work);
			mode_selection_next(partner, ms);
		}
	}
	mutex_unlock(&partner->mode_sequence_lock);
}
EXPORT_SYMBOL_GPL(mode_selection_complete);

static int mode_selection_activate_altmode(struct device *dev, void *data)
{
	struct mode_selection_state *ms = (struct mode_selection_state *)data;
	int error = -ENODEV;
	int ret = 0;

	if (!strcmp(dev->type->name, ALTERNATE_MODE_DEVICE_TYPE_NAME)) {
		struct typec_altmode *alt = to_typec_altmode(dev);

		if (ms->mode == typec_svid_to_altmode(alt->svid)) {
			if (alt->ops && alt->ops->activate)
				error = alt->ops->activate(alt, ms->enter);
			else
				error = -EOPNOTSUPP;
			ret = 1;
		}
	}

	if (ms->enter)
		ms->error = error;

	return ret;
}

/**
 * mode_selection_work() - Activate entry into the upcoming mode
 * @work: work structure
 *
 * This function works in conjunction with `mode_selection_next()`.
 * It attempts to activate the next mode in the selection sequence.
 *
 * If the mode activation (`mode_selection_activate_altmode()`) fails,
 * `mode_selection_next()` will be called to initiate a new selection cycle.
 *
 * Otherwise, the state is set to MS_STATE_INPROGRESS, and
 * `mode_selection_work()` is scheduled for a subsequent entry after a timeout
 * period. The alternate mode driver is expected to call back with the actual
 * mode entry result. Upon this callback, `mode_selection_next()` will determine
 * the subsequent mode and re-schedule `mode_selection_work()`.
 */
static void mode_selection_work(struct work_struct *work)
{
	struct typec_partner *partner = container_of(work, struct typec_partner,
						  mode_selection_work.work);
	struct mode_selection_state *ms;

	mutex_lock(&partner->mode_sequence_lock);
	if (kfifo_peek(&partner->mode_sequence, &ms)) {
		if (ms->state == MS_STATE_INPROGRESS) {
			ms->state = MS_STATE_TIMEOUT;
			mode_selection_next(partner, ms);
		} else {
			if (ms->enter)
				ms->attempt_count++;

			device_for_each_child(&partner->dev, ms,
				mode_selection_activate_altmode);

			if (ms->enter && !ms->error) {
				ms->state = MS_STATE_INPROGRESS;
				schedule_delayed_work(&partner->mode_selection_work,
					msecs_to_jiffies(mode_selection_timeout));
			} else
				mode_selection_next(partner, ms);
		}
	}
	mutex_unlock(&partner->mode_sequence_lock);
}

static void mode_selection_clear_results(struct typec_partner *partner)
{
	struct typec_port *port = to_typec_port(partner->dev.parent);
	struct mode_selection_state *ms;

	list_for_each_entry(ms, &port->mode_list, list) {
		ms->enter = true;
		ms->state = MS_STATE_IDLE;
		ms->error = 0;
		ms->attempt_count = 0;
	}

	kfifo_reset(&partner->mode_sequence);
	partner->active_mode = NULL;
}

void typec_mode_selection_add_partner(struct typec_partner *partner)
{
	INIT_KFIFO(partner->mode_sequence);
	mutex_init(&partner->mode_sequence_lock);
	mode_selection_clear_results(partner);
	INIT_DELAYED_WORK(&partner->mode_selection_work, mode_selection_work);
}

void typec_mode_selection_remove_partner(struct typec_partner *partner)
{
	mutex_lock(&partner->mode_sequence_lock);
	kfifo_reset(&partner->mode_sequence);
	mutex_unlock(&partner->mode_sequence_lock);

	cancel_delayed_work_sync(&partner->mode_selection_work);
	mutex_destroy(&partner->mode_sequence_lock);
}

int typec_mode_selection_is_pending(struct typec_partner *partner)
{
	if (!kfifo_is_empty(&partner->mode_sequence))
		return -EINPROGRESS;
	else if (partner->active_mode)
		return -EALREADY;
	return 0;
}

static int mode_selection_check_partner(struct device *dev, void *data)
{
	enum typec_mode_type *mode = (enum typec_mode_type *)data;

	if (!strcmp(dev->type->name, ALTERNATE_MODE_DEVICE_TYPE_NAME)) {
		struct typec_altmode *alt = to_typec_altmode(dev);

		if (*mode == typec_svid_to_altmode(alt->svid))
			return 1;
	}

	return 0;
}

static bool mode_selection_check_cable(struct typec_partner *partner,
	enum typec_mode_type mode)
{
	struct typec_port *port = to_typec_port(partner->dev.parent);
	struct typec_cable *cable = typec_cable_get(port);
	bool supported = false;

	if (cable) {
		const u32 id_header = cable->identity->id_header;
		const u32 vdo1 = cable->identity->vdo[0];
		const u32 type = PD_IDH_PTYPE(id_header);
		const u32 speed = VDO_TYPEC_CABLE_SPEED(vdo1);

		if (type == IDH_PTYPE_PCABLE)
			supported = (speed > CABLE_USB2_ONLY);
		else if (type == IDH_PTYPE_ACABLE) {
			const u32 vdo2 = cable->identity->vdo[1];
			const u32 version = VDO_TYPEC_CABLE_VERSION(vdo1);
			const bool usb4_support = VDO_TYPEC_CABLE_USB4_SUPP(vdo2);
			const bool modal_support = PD_IDH_MODAL_SUPP(id_header);

			if (mode == TYPEC_ALTMODE_DP)
				supported = modal_support;
			else if (mode == TYPEC_ALTMODE_TBT)
				supported = true;
			else if (mode == TYPEC_ALTMODE_USB4) {
				if (version == CABLE_VDO_VER1_3)
					supported = usb4_support;
				else
					supported = modal_support;
			}
		} else {
			/*
			 * Some USB devices supporting DisplayPort lack valid cable VDO.
			 * Allow only DP mode in this case.
			 */
			if (mode == TYPEC_ALTMODE_DP)
				supported = true;
		}

		typec_cable_put(cable);
	}

	return supported;
}

/**
 * typec_mode_selection_start() - Starts the mode selection process.
 * @partner: pointer to the partner structure
 *
 * This function populates a 'mode_sequence' FIFO with pointers to
 * `struct mode_selection_state` instances. The sequence is generated based on
 * partner/cable capabilities and prioritized according to the port's settings.
 */
int typec_mode_selection_start(struct typec_partner *partner)
{
	struct typec_port *port = to_typec_port(partner->dev.parent);
	struct mode_selection_state *ms;
	int ret;

	mutex_lock(&partner->mode_sequence_lock);

	ret = typec_mode_selection_is_pending(partner);

	if (!ret) {
		mode_selection_clear_results(partner);

		list_for_each_entry(ms, &port->mode_list, list) {
			const bool cable_supported = mode_selection_check_cable(
					partner, ms->mode);
			const bool partner_supported = device_for_each_child(
				&partner->dev, &ms->mode, mode_selection_check_partner);

			if (partner_supported) {
				if (cable_supported)
					kfifo_put(&partner->mode_sequence, ms);
				else
					ms->state = MS_STATE_CABLE_FAILED;
			}
		}

		if (kfifo_peek(&partner->mode_sequence, &ms))
			schedule_delayed_work(&partner->mode_selection_work, 0);
	}

	mutex_unlock(&partner->mode_sequence_lock);

	return ret;
}

/**
 * typec_mode_selection_reset() - Reset the mode selection process.
 * @partner: pointer to the partner structure
 *
 * This function cancels ongoing mode selection and exits the currently active
 * mode, if present.
 * It returns -EINPROGRESS when a mode exit is already scheduled, or a mode
 * entry is ongoing, indicating that the reset cannot immediately complete.
 */
int typec_mode_selection_reset(struct typec_partner *partner)
{
	struct mode_selection_state *ms;

	mutex_lock(&partner->mode_sequence_lock);
	if (kfifo_peek(&partner->mode_sequence, &ms)) {
		kfifo_reset(&partner->mode_sequence);

		if (!ms->enter || ms->state != MS_STATE_IDLE) {
			ms->attempt_count = mode_entry_attempts;
			kfifo_put(&partner->mode_sequence, ms);
			mutex_unlock(&partner->mode_sequence_lock);

			return -EINPROGRESS;
		}
	}

	if (partner->active_mode) {
		partner->active_mode->enter = false;
		device_for_each_child(&partner->dev, partner->active_mode,
				mode_selection_activate_altmode);
	}
	mode_selection_clear_results(partner);
	mutex_unlock(&partner->mode_sequence_lock);

	return 0;
}

int typec_mode_selection_get_state(struct typec_partner *partner, char *buf)
{
	struct typec_port *port = to_typec_port(partner->dev.parent);
	struct mode_selection_state *ms, *running_ms;
	ssize_t count = 0;

	mutex_lock(&partner->mode_sequence_lock);
	if (!kfifo_peek(&partner->mode_sequence, &running_ms))
		running_ms = NULL;

	list_for_each_entry(ms, &port->mode_list, list) {
		const bool partner_supported = device_for_each_child(
			&partner->dev, &ms->mode, mode_selection_check_partner);

		if (partner_supported) {
			if (ms->state == MS_STATE_ACTIVE)
				count += sysfs_emit_at(buf, count, "[%s] ",
					mode_names[ms->mode]);
			else if (ms == running_ms)
				count += sysfs_emit_at(buf, count, "(%s) ",
					mode_names[ms->mode]);
			else
				count += sysfs_emit_at(buf, count, "%s ",
					mode_names[ms->mode]);
		}
	}
	mutex_unlock(&partner->mode_sequence_lock);

	if (count)
		count += sysfs_emit_at(buf, count, "\n");

	return count;
}
