/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM alarmtimer

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_ALARMTIMER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_ALARMTIMER_H

#include <trace/hooks/vendor_hooks.h>

struct alarm;
DECLARE_HOOK(android_vh_alarmtimer_enqueue,
	TP_PROTO(struct alarm *alarm),
	TP_ARGS(alarm));

#endif /* _TRACE_HOOK_ALARMTIMER_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
