/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM vmscan

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_VMSCAN_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_VMSCAN_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_tune_reclaim_type,
	TP_PROTO(int *tune_writepage),
	TP_ARGS(tune_writepage));

#endif /* _TRACE_HOOK_VMSCAN_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
