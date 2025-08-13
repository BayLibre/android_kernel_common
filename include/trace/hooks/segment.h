/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM segment

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_SEGMENT_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_READ_SEGMENT_H

#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_segment_border_secno,
	TP_PROTO(unsigned int *hint),
	TP_ARGS(hint));

#endif /* _TRACE_HOOK_TIMER_H */
/* This part must be outside protection */
#include <trace/define_trace.h>