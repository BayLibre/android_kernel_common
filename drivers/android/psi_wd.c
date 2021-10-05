// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Google, Inc.
 */

#define pr_fmt(fmt) "psi_wd: " fmt

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/oom.h>
#include <linux/psi.h>
#include <linux/timer.h>
#include <trace/hooks/psi.h>

#define MAX_TRIGGER_COUNT 3

static bool is_active;
static int wd_timeout_ms = 2000;
static struct pid *wd_target_pid;
static DEFINE_SPINLOCK(wd_lock);
static struct timer_list wdog_timer;
static struct psi_trigger *triggers[MAX_TRIGGER_COUNT];

static void watchdog_fn(struct timer_list *t)
{
	const gfp_t gfp_mask = GFP_KERNEL;
	struct oom_control oc = {
		.zonelist = node_zonelist(first_memory_node, gfp_mask),
		.nodemask = NULL,
		.memcg = NULL,
		.gfp_mask = gfp_mask,
		.order = -1,
	};

	pr_err("PSI watchdog fired, generating oom-kill!\n");
	mutex_lock(&oom_lock);
	if (!out_of_memory(&oc))
		pr_warn("OOM request ignored. No task eligible\n");
	mutex_unlock(&oom_lock);

	spin_lock(&wd_lock);
	/* Re-activate the timer until the target process disables it */
	if (is_active)
		mod_timer(&wdog_timer,
			  jiffies + msecs_to_jiffies(wd_timeout_ms));
	spin_unlock(&wd_lock);
}

static void set_timer(bool active)
{
	spin_lock(&wd_lock);
	if (active != is_active) {
		if (active)
			mod_timer(&wdog_timer,
				  jiffies + msecs_to_jiffies(wd_timeout_ms));
		else
			del_timer(&wdog_timer);
		is_active = active;
	}
	spin_unlock(&wd_lock);
}

void psi_wd_start(struct psi_trigger *t)
{
	int i;

	for (i = 0; i < MAX_TRIGGER_COUNT; i++) {
		if (triggers[i] == t) {
			set_timer(1);
			break;
		}
	}
}

/*
 * Verifie that the target is valid. Perform cleanup and reset wd_target_pid
 * if the target is set but not valid anymore (crashed or got killed). Returns
 * true only if the target is set and is valid.
 */
static bool validate_target(void)
{
	if (!wd_target_pid)
		return false;

	if (pid_has_task(wd_target_pid, PIDTYPE_PID))
		return true;

	/* Target process is gone, perform cleanup */
	put_pid(wd_target_pid);
	wd_target_pid = NULL;
	memset(triggers, 0, sizeof(triggers));

	return false;
}

void psi_wd_stop(struct task_struct *task, struct psi_trigger *t)

{
	int i;

	if (!validate_target())
		return;

	if (task != pid_task(wd_target_pid, PIDTYPE_PID))
		return;

	for (i = 0; i < MAX_TRIGGER_COUNT; i++) {
		if (triggers[i] == t) {
			set_timer(0);
			break;
		}
		if (!triggers[i]) {
			/* New trigger, start monitoring */
			triggers[i] = t;
			break;
		}
	}
}

static int wd_target_pidfd_set(const char *val, const struct kernel_param *kp)
{
	long pidfd;
	struct pid *pid;
	unsigned int f_flags;

	if (!val)
		return -EINVAL;

	if (kstrtol(val, 10, &pidfd) != 0)
		return -EINVAL;

	pid = pidfd_get_pid(pidfd, &f_flags);
	if (IS_ERR(pid))
		return PTR_ERR(pid);

	if (wd_target_pid) {
		put_pid(wd_target_pid);
		memset(triggers, 0, sizeof(triggers));
	}

	wd_target_pid = pid;

	return 0;
}

static int wd_target_pidfd_get(char *buf, const struct kernel_param *kp)
{
	const char *res;

	if (validate_target())
		res = "<set>";
	else
		res = "<empty>";

	return sprintf(buf, "%s\n", res);
}

static int active_set(const char *val, const struct kernel_param *kp)
{
	long activate;
	int ret;

	if (!val)
		return -EINVAL;

	ret = kstrtol(val, 10, &activate);
	if (ret != 0 || activate < 0 || activate > 1)
		return -EINVAL;

	set_timer(activate);

	return 0;
}

static const struct kernel_param_ops wd_target_pidfd_ops = {
	.set = wd_target_pidfd_set,
	.get = wd_target_pidfd_get,
};

static const struct kernel_param_ops active_ops = {
	.set = active_set,
	.get = param_get_int,
};

module_param_cb(pidfd, &wd_target_pidfd_ops, NULL, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(pidfd, "Target PIDFD for the watchdog");

module_param_cb(active, &active_ops, &is_active, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(active, "Watchdog active flag");

module_param_named(timeout_ms, wd_timeout_ms, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(timeout_ms, "Timeout for the target process to process PSI event");

static int __init psi_wd_init(void)
{
	timer_setup(&wdog_timer, watchdog_fn, TIMER_DEFERRABLE);

	return 0;
}
static void __exit psi_wd_exit(void)
{
	del_timer_sync(&wdog_timer);
	if (wd_target_pid)
		put_pid(wd_target_pid);
}

module_init(psi_wd_init);
module_exit(psi_wd_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("PSI watchdog driver");
