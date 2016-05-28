/*
 * rtc time printing utility functions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kdebug.h>
#include <linux/reboot.h>
#include <linux/rtc.h>

void rtc_show_time(const char *prefix_msg)
{
	struct timespec ts;
	struct rtc_time tm;

	getnstimeofday(&ts);
	rtc_time64_to_tm(ts.tv_sec, &tm);
	pr_info("%s %d-%02d-%02d %02d:%02d:%02d.%09lu UTC\n",
		prefix_msg ? prefix_msg : "Time:",
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec);
}
EXPORT_SYMBOL(rtc_show_time);

static int rtc_show_time_die_notify(struct notifier_block *self,
		unsigned long event, void *data)
{
	if (event != DIE_OOPS)
		return NOTIFY_DONE;
	rtc_show_time("Oops");
	return NOTIFY_DONE;
}

static struct notifier_block rtc_show_time_die_nb = {
	.notifier_call = rtc_show_time_die_notify,
	.priority = 0,
};

static int rtc_show_time_reboot_notify(struct notifier_block *self,
		unsigned long event, void *data)
{
	const char *txt;

	switch (event) {
	case SYS_RESTART:
		txt = "Restart";
		break;
	case SYS_HALT:
		txt = "Halt";
		break;
	case SYS_POWER_OFF:
		txt = "Power-Off";
		break;
	default:
		return NOTIFY_DONE;
	}
	rtc_show_time(txt);
	return NOTIFY_DONE;
}

static struct notifier_block rtc_show_time_reboot_nb = {
	.notifier_call = rtc_show_time_reboot_notify,
	.priority = 0,
};

static __init int init_rtc_show_time(void)
{
	int ret;

	ret = register_die_notifier(&rtc_show_time_die_nb);
	if (ret)
		pr_warn("Failed to register rtc_show_time die notifier\n");
	ret = register_reboot_notifier(&rtc_show_time_reboot_nb);
	if (ret)
		pr_warn("Failed to register rtc_show_time reboot notifier\n");

	return ret;
}
/* rtc driver needs to be loaded before this is truly functional */
late_initcall(init_rtc_show_time);
