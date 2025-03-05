/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM rtc

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_RTC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_RTC_H

#include <trace/hooks/vendor_hooks.h>

struct rtc_timer;
DECLARE_HOOK(android_vh_rtc_timer_enqueue,
	TP_PROTO(struct rtc_timer *timer),
	TP_ARGS(timer));

#endif /* _TRACE_HOOK_RTC_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
