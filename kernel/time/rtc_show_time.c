/*
 * rtc time printing utility functions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/rtc.h>

void rtc_show_time(const char *prefix_msg)
{
#if defined(CONFIG_RTC_SHOW_TIME_MONOTONIC) /* dmesg is in monotonic */
	pr_info("%s\n", prefix_msg ? prefix_msg : "Time:");
#elif defined(CONFIG_RTC_SHOW_TIME_BOOTTIME)
	struct timespec64 ts;

	getboottime64(&ts);
	pr_info("%s %lu.%09lu B\n",
		prefix_msg ? prefix_msg : "Time:", ts.tv_sec, ts.tv_nsec);
#else /* realtime */
	struct timespec64 ts;

	getnstimeofday64(&ts);
	pr_info("%s %lu.%09lu UTC\n",
		prefix_msg ? prefix_msg : "Time:", ts.tv_sec, ts.tv_nsec);
#endif
}
EXPORT_SYMBOL(rtc_show_time);
