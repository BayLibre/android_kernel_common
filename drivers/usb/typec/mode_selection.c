// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC.
 */

#include <linux/types.h>
#include <linux/list_sort.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

#include "class.h"
#include "bus.h"

/**
 * struct mode_state - State tracking for a specific Type-C mode
 * @altmode:
 * @error:
 * @list: List head to link this mode state into a prioritized list
 */
struct mode_state {
	struct typec_altmode *altmode;
	int error;
	struct list_head list;
};

struct mode_selection {
	struct list_head mode_list;
	struct mutex lock;
	struct delayed_work work;
	struct typec_partner *partner;
	u16 active_svid;
	unsigned int timeout;
	unsigned int delay;
};

static void mode_list_clean(struct mode_selection *sel)
{
	struct mode_state *ms, *tmp;

	list_for_each_entry_safe(ms, tmp, &sel->mode_list, list) {
		list_del(&ms->list);
		kfree(ms);
	}
}

void typec_altmode_state_update(struct typec_partner *partner, const u16 svid,
	const int error)
{
	struct mode_selection *sel = partner->sel;
	struct mode_state *ms;

	if (sel) {
		mutex_lock(&sel->lock);
		ms = list_first_entry_or_null(&sel->mode_list, struct mode_state, list);
		if (ms && ms->altmode->svid == svid) {
			ms->error = error;
			cancel_delayed_work(&sel->work);
			schedule_delayed_work(&sel->work, msecs_to_jiffies(sel->delay));
		}
		if(error)
			sel->active_svid = 0;
		else
			sel->active_svid = svid;
		mutex_unlock(&sel->lock);
	}
}
EXPORT_SYMBOL_GPL(typec_altmode_state_update);

static int mode_selection_activate(struct mode_selection *sel,
		const struct typec_altmode *altmode, const int enter)

	__must_hold(&sel->lock)
{
	int ret = -EOPNOTSUPP;

	if (altmode->ops && altmode->ops->activate) {
		mutex_unlock(&sel->lock);
		ret = altmode->ops->activate((void *)altmode, enter);
		mutex_lock(&sel->lock);
	}
	return ret;
}

static int exit_active_mode(struct device *dev, void *data)
{
	if (is_typec_altmode(dev)) {
		struct mode_selection *sel = (struct mode_selection *)data;
		struct typec_altmode *altmode = to_typec_altmode(dev);

		if (sel->active_svid == altmode->svid) {
			const int ret = mode_selection_activate(sel, altmode, 0);
			if (!ret)
				return 1;
			return ret;
		}
	}
	return 0;
}

/**
 * mode_selection_work_fn() - Activate entry into the upcoming mode
 * @work: work structure
 *
 * This function works in conjunction with `typec_altmode_state_update()`.
 * It attempts to activate the next mode in the selection sequence.
 *
 * If the mode activation fails, `mode_selection_next()` will be called to
 * initiate a new selection cycle.
 *
 * Otherwise, the state is set to MS_STATE_INPROGRESS, and
 * `mode_selection_work_fn()` is scheduled for a subsequent entry after a timeout
 * period. The alternate mode driver is expected to call back with the actual
 * mode entry result. Upon this callback, `mode_selection_next()` will determine
 * the subsequent mode and re-schedule `mode_selection_work_fn()`.
 */
static void mode_selection_work_fn(struct work_struct *work)
{
	struct mode_selection *sel = container_of(work,
				struct mode_selection, work.work);
	struct mode_state *ms;
	unsigned int delay = sel->delay;
	int res;

	mutex_lock(&sel->lock);

	ms = list_first_entry_or_null(&sel->mode_list, struct mode_state, list);
	if (!ms) {
		mutex_unlock(&sel->lock);
		return;
	}

	if (sel->active_svid == ms->altmode->svid) {
		dev_dbg(&sel->partner->dev, "%s altmode is active\n",
				ms->altmode->desc);
		mode_list_clean(sel);
	} else if (sel->active_svid != 0) {
		res = device_for_each_child(&sel->partner->dev, sel, exit_active_mode);
		if (res <= 0) {
			dev_dbg(&sel->partner->dev, "enable to exit %x altmode\n",
					sel->active_svid);
			mode_list_clean(sel);
		}
	} else if (ms->error) {
		dev_dbg(&sel->partner->dev, "%s: entry error %pe\n",
				ms->altmode->desc, ERR_PTR(ms->error));
		mode_selection_activate(sel, ms->altmode, 0);
		list_del(&ms->list);
		kfree(ms);
	} else {
		ms->error = -ETIMEDOUT;
		res = mode_selection_activate(sel, ms->altmode, 1);
		if (res) {
			dev_dbg(&sel->partner->dev, "%s: activation error %pe\n",
					ms->altmode->desc, ERR_PTR(res));
			list_del(&ms->list);
			kfree(ms);
		} else
			delay = sel->timeout;
	}

	if (!list_empty(&sel->mode_list))
		schedule_delayed_work(&sel->work, msecs_to_jiffies(delay));
	mutex_unlock(&sel->lock);
}

static int compare_priorities(void *priv,
	const struct list_head *a, const struct list_head *b)
{
	const struct mode_state *msa = container_of(a, struct mode_state, list);
	const struct mode_state *msb = container_of(b, struct mode_state, list);
	const struct typec_altmode *pdeva = typec_altmode_get_partner(msa->altmode);
	const struct typec_altmode *pdevb = typec_altmode_get_partner(msb->altmode);

	if (pdeva->priority < pdevb->priority)
		return -1;
	return 1;
}

static int mode_add_to_list(struct device *dev, void *data)
{
	struct list_head *list = (struct list_head *)data;
	struct mode_state *ms;

	if (is_typec_altmode(dev)) {
		struct typec_altmode *altmode = to_typec_altmode(dev);
		const struct typec_altmode *pdev = typec_altmode_get_partner(altmode);

		if (pdev && altmode->ops && altmode->ops->activate) {
			ms = kzalloc(sizeof(struct mode_state), GFP_KERNEL);
			if (!ms)
				return -ENOMEM;

			ms->altmode = altmode;
			INIT_LIST_HEAD(&ms->list);
			list_add_tail(&ms->list, list);
		}
	}
	return 0;
}

/**
 * typec_mode_selection_start() - Starts the mode selection process.
 * @partner: pointer to the partner structure
 * @delay:
 * @timeout:
 *
 * This function populates mode_list with pointers to
 * `struct mode_state` instances. The sequence is prioritized
 * according to the port's settings.
 */
int typec_mode_selection_start(struct typec_partner *partner,
	const unsigned int delay, const unsigned int timeout)
{
	int ret;
	struct mode_selection *sel;

	if (partner->sel)
		return -EALREADY;

	sel = kzalloc(sizeof(struct mode_selection), GFP_KERNEL);
	if (!sel)
		return -ENOMEM;

	INIT_LIST_HEAD(&sel->mode_list);
	ret = device_for_each_child(
		&partner->dev, &sel->mode_list, mode_add_to_list);

	if (!ret && !list_empty(&sel->mode_list)) {
		sel->partner = partner;
		sel->delay = delay;
		sel->timeout = timeout;

		list_sort(NULL, &sel->mode_list, compare_priorities);
		mutex_init(&sel->lock);
		partner->sel = sel;
		INIT_DELAYED_WORK(&sel->work, mode_selection_work_fn);
		schedule_delayed_work(&sel->work,
			msecs_to_jiffies(delay));
	} else
		mode_list_clean(sel);

	return ret;
}
EXPORT_SYMBOL_GPL(typec_mode_selection_start);

void typec_mode_selection_delete(struct typec_partner *partner)
{
	struct mode_selection *sel = partner->sel;

	if (sel) {
		partner->sel = NULL;
		cancel_delayed_work_sync(&sel->work);
		mode_list_clean(sel);
		mutex_destroy(&sel->lock);
		kfree(sel);
	}
}
EXPORT_SYMBOL_GPL(typec_mode_selection_delete);
