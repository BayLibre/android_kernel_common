// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC.
 */

#include <linux/list_sort.h>

#include "mode_selection.h"
#include "class.h"
#include "bus.h"

/* Timeout for a mode entry attempt, ms */
static const unsigned int mode_selection_timeout = 4000;
/* Delay between mode entry/exit attempts, ms */
static const unsigned int mode_selection_delay = 1000;
/* Maximum retries for mode entry on busy status */
static const unsigned int mode_entry_attempts = 4;

/**
 * enum ms_state - Specific mode selection states
 * @MS_STATE_IDLE: The mode entry process has not started
 * @MS_STATE_INPROGRESS: The mode entry process is currently underway
 * @MS_STATE_ACTIVE: The mode has been successfully entered
 * @MS_STATE_TIMEOUT: Mode entry failed due to a timeout
 * @MS_STATE_FAILED: The mode driver reported the error
 */
enum ms_state {
	MS_STATE_IDLE = 0,
	MS_STATE_INPROGRESS,
	MS_STATE_ACTIVE,
	MS_STATE_TIMEOUT,
	MS_STATE_FAILED,
};

/**
 * struct mode_selection_state - State tracking for a specific Type-C mode
 * @svid: The Standard or Vendor ID (SVID) for this alternate mode
 * @name: Name of the alternate mode
 * @priority: The mode priority. Lower values indicate a more preferred mode
 * @enter: Flag indicating if the driver is currently attempting to enter or
 * exit the mode
 * @attempt_count: Number of times the driver has attempted to enter the mode
 * @state: The current mode selection state
 * @error: The outcome of the last attempt to enter the mode
 * @list: List head to link this mode state into a prioritized list
 */
struct mode_selection_state {
	u16 svid;
	const char *name;
	u8 priority;
	bool enter;
	int attempt_count;
	enum ms_state state;
	int error;
	struct list_head list;
};

static int increment_duplicated_priority(struct device *dev, void *data)
{
	if (is_typec_altmode(dev)) {
		struct typec_altmode **alt_target = (struct typec_altmode **)data;
		struct typec_altmode *alt = to_typec_altmode(dev);

		if (alt != *alt_target && alt->priority == (*alt_target)->priority) {
			alt->priority++;
			*alt_target = alt;
			return 1;
		}
	}
	return 0;
}

static int find_duplicated_priority(struct device *dev, void *data)
{
	if (is_typec_altmode(dev)) {
		struct typec_altmode **alt_target = (struct typec_altmode **)data;
		struct typec_altmode *alt = to_typec_altmode(dev);

		if (alt != *alt_target && alt->priority == (*alt_target)->priority)
			return 1;
	}
	return 0;
}

int typec_mode_set_priority(struct typec_altmode *alt, const u8 priority)
{
	struct typec_port *port = to_typec_port(alt->dev.parent);
	const u8 old_priority = alt->priority;
	int res = 1;

	alt->priority = priority;
	while (res) {
		res = device_for_each_child(&port->dev, &alt, find_duplicated_priority);
		if (res) {
			alt->priority++;
			if (alt->priority == 0) {
				alt->priority = old_priority;
				return -EOVERFLOW;
			}
		}
	}

	res = 1;
	alt->priority = priority;
	while (res)
		res = device_for_each_child(&port->dev, &alt,
				increment_duplicated_priority);

	return 0;
}

static void mode_list_clean(struct typec_partner *partner)
{
	struct mode_selection_state *ms, *tmp;

	list_for_each_entry_safe(ms, tmp, &partner->mode_list, list) {
		list_del(&ms->list);
		kfree(ms);
	}
}

/**
 * mode_selection_next() - Process mode selection results and schedule next
 * action
 * @partner: pointer to the partner structure
 * @ms: pointer to active mode_selection_state object that is on top in
 * mode_list.
 *
 * The mutex protecting mode_list must be held by the caller when invoking this
 * function.
 *
 * This function evaluates the outcome of the previous mode entry or exit
 * attempt. Based on this result, it determines the next mode to process and
 * schedules `mode_selection_work_fn()` if further actions are required.
 *
 * If the previous mode entry was successful, the mode selection sequence is
 * considered complete for the current cycle.
 *
 * If the previous mode entry failed, this function schedules
 * `mode_selection_work_fn()` to attempt exiting the mode that was partially
 * activated but not fully entered.
 *
 * If the previous operation was an exit (after a failed entry attempt),
 * the internal list of candidate modes is advanced to determine the next mode
 * to enter.
 */
static void mode_selection_next(
	struct typec_partner *partner, struct mode_selection_state *ms)

	__must_hold(&partner->mode_list_lock)
{
	if (!ms->enter) {
		list_del(&ms->list);
		kfree(ms);
	} else if (ms->state == MS_STATE_INPROGRESS && !ms->error) {
		list_del(&ms->list);
		mode_list_clean(partner);

		ms->state = MS_STATE_ACTIVE;
		list_add_tail(&ms->list, &partner->mode_list);
	} else {
		if (ms->error) {
			ms->state = MS_STATE_FAILED;
			dev_dbg(&partner->dev, "%s: entry mode error %pe\n",
				ms->name, ERR_PTR(ms->error));
		}
		if (ms->error != -EBUSY || ms->attempt_count >= mode_entry_attempts)
			ms->enter = false;
	}

	ms = list_first_entry_or_null(
		&partner->mode_list, struct mode_selection_state, list);
	if (ms && ms->state != MS_STATE_ACTIVE)
		schedule_delayed_work(&partner->mode_selection_work,
			msecs_to_jiffies(mode_selection_delay));
}

void typec_altmode_entry_complete(struct typec_altmode *altmode,
				const int error)
{
	const struct typec_altmode *pdev = typec_altmode_get_partner(altmode);
	struct typec_partner *partner;
	struct mode_selection_state *ms;

	if (!pdev)
		return;
	partner = to_typec_partner(pdev->dev.parent);

	mutex_lock(&partner->mode_list_lock);

	ms = list_first_entry_or_null(
		&partner->mode_list, struct mode_selection_state, list);
	if (ms) {
		if (ms->svid == altmode->svid && ms->state == MS_STATE_INPROGRESS) {
			ms->error = error;
			cancel_delayed_work(&partner->mode_selection_work);
			mode_selection_next(partner, ms);
		}
	}

	mutex_unlock(&partner->mode_list_lock);
}
EXPORT_SYMBOL_GPL(typec_altmode_entry_complete);

static int mode_selection_activate_altmode(struct device *dev, void *data)
{
	struct mode_selection_state *ms = (struct mode_selection_state *)data;
	int error = -ENODEV;
	int ret = 0;

	if (is_typec_altmode(dev)) {
		struct typec_altmode *altmode = to_typec_altmode(dev);

		if (ms->svid == altmode->svid) {
			if (altmode->ops && altmode->ops->activate)
				error = altmode->ops->activate(altmode, ms->enter);
			else
				error = -EOPNOTSUPP;
			ret = 1;
		}
	}

	if (ms->enter) {
		ms->attempt_count++;
		ms->error = error;
	}

	return ret;
}

/**
 * mode_selection_work_fn() - Activate entry into the upcoming mode
 * @work: work structure
 *
 * This function works in conjunction with `mode_selection_next()`.
 * It attempts to activate the next mode in the selection sequence.
 *
 * If the mode activation (`mode_selection_activate_altmode()`) fails,
 * `mode_selection_next()` will be called to initiate a new selection cycle.
 *
 * Otherwise, the state is set to MS_STATE_INPROGRESS, and
 * `mode_selection_work_fn()` is scheduled for a subsequent entry after a timeout
 * period. The alternate mode driver is expected to call back with the actual
 * mode entry result. Upon this callback, `mode_selection_next()` will determine
 * the subsequent mode and re-schedule `mode_selection_work_fn()`.
 */
static void mode_selection_work_fn(struct work_struct *work)
{
	struct typec_partner *partner = container_of(work, struct typec_partner,
						  mode_selection_work.work);
	struct mode_selection_state *ms;

	mutex_lock(&partner->mode_list_lock);

	ms = list_first_entry_or_null(
		&partner->mode_list, struct mode_selection_state, list);
	if (ms) {
		if (ms->state == MS_STATE_INPROGRESS) {
			ms->state = MS_STATE_TIMEOUT;
			mode_selection_next(partner, ms);
		} else {
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

	mutex_unlock(&partner->mode_list_lock);
}

void typec_mode_selection_add_partner(struct typec_partner *partner)
{
	INIT_LIST_HEAD(&partner->mode_list);
	mutex_init(&partner->mode_list_lock);
	INIT_DELAYED_WORK(&partner->mode_selection_work, mode_selection_work_fn);
}

void typec_mode_selection_remove_partner(struct typec_partner *partner)
{
	mutex_lock(&partner->mode_list_lock);
	mode_list_clean(partner);
	mutex_unlock(&partner->mode_list_lock);

	cancel_delayed_work_sync(&partner->mode_selection_work);
	mutex_destroy(&partner->mode_list_lock);
}

bool typec_mode_selection_is_pending(struct typec_partner *partner)
{
	struct mode_selection_state *ms = list_first_entry_or_null(
			&partner->mode_list, struct mode_selection_state, list);

	return ms != NULL && ms->state != MS_STATE_ACTIVE;
}

static int compare_priorities(void *priv,
	const struct list_head *a, const struct list_head *b)
{
	struct mode_selection_state *msa =
			container_of(a, struct mode_selection_state, list);
	struct mode_selection_state *msb =
			container_of(b, struct mode_selection_state, list);

	if (msa->priority < msb->priority)
		return -1;
	return 1;
}

static int mode_add_to_list(struct device *dev, void *data)
{
	struct list_head *list = (struct list_head *)data;
	struct mode_selection_state *ms;

	if (is_typec_altmode(dev)) {
		struct typec_altmode *altmode = to_typec_altmode(dev);
		const struct typec_altmode *pdev = typec_altmode_get_partner(altmode);

		if (pdev) {
			ms = kzalloc(sizeof(struct mode_selection_state), GFP_KERNEL);
			if (!ms)
				return -ENOMEM;

			ms->svid = altmode->svid;
			ms->name = altmode->desc;
			ms->priority = pdev->priority;
			ms->enter = true;
			INIT_LIST_HEAD(&ms->list);
			list_add_tail(&ms->list, list);
		}
	}

	return 0;
}

/**
 * typec_mode_selection_start() - Starts the mode selection process.
 * @partner: pointer to the partner structure
 *
 * This function populates mode_list with pointers to
 * `struct mode_selection_state` instances. The sequence is generated based on
 * partner capabilities and prioritized according to the port's settings.
 */
int typec_mode_selection_start(struct typec_partner *partner)
{
	int ret;

	if (typec_mode_selection_is_pending(partner))
		return -EALREADY;

	ret = typec_mode_selection_reset(partner);
	if (ret)
		return ret;

	mutex_lock(&partner->mode_list_lock);

	ret = device_for_each_child(
		&partner->dev, &partner->mode_list, mode_add_to_list);

	if (ret)
		mode_list_clean(partner);
	else if (!list_empty(&partner->mode_list)) {
		list_sort(NULL, &partner->mode_list, compare_priorities);
		schedule_delayed_work(&partner->mode_selection_work,
			msecs_to_jiffies(mode_selection_delay));
	}

	mutex_unlock(&partner->mode_list_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(typec_mode_selection_start);

/**
 * typec_mode_selection_reset() - Reset the mode selection process.
 * @partner: pointer to the partner structure
 *
 * This function cancels ongoing mode selection and exits the currently active
 * mode, if present.
 * It returns -EINPROGRESS when a mode entry is ongoing, indicating that the
 * reset cannot immediately complete.
 */
int typec_mode_selection_reset(struct typec_partner *partner)
{
	struct mode_selection_state *ms;
	int ret = 0;

	mutex_lock(&partner->mode_list_lock);

	ms = list_first_entry_or_null(
		&partner->mode_list, struct mode_selection_state, list);
	if (ms) {
		if (ms->state == MS_STATE_ACTIVE) {
			ms->enter = false;
			device_for_each_child(&partner->dev, ms,
					mode_selection_activate_altmode);
		} else if (ms->state != MS_STATE_IDLE) {
			list_del(&ms->list);
			mode_list_clean(partner);

			ms->attempt_count = mode_entry_attempts;
			list_add(&ms->list, &partner->mode_list);

			ret = -EINPROGRESS;
		}

		if (!ret)
			mode_list_clean(partner);
	}

	mutex_unlock(&partner->mode_list_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(typec_mode_selection_reset);

int typec_mode_selection_get_state(struct typec_partner *partner, char *buf)
{
	struct mode_selection_state *ms;
	ssize_t count = 0;

	mutex_lock(&partner->mode_list_lock);

	ms = list_first_entry_or_null(&partner->mode_list, struct mode_selection_state, list);
	if (ms) {
		if (ms->state == MS_STATE_ACTIVE)
			count = sysfs_emit_at(buf, count, "[%s]\n", ms->name);
		else
			count = sysfs_emit_at(buf, count, "%s\n", ms->name);
	}

	mutex_unlock(&partner->mode_list_lock);

	return count;
}
