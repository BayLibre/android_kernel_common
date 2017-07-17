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
