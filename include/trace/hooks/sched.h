/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM sched
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_VENDOR_HOOKS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_VENDOR_HOOKS_H

#include <linux/tracepoint.h>

/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */

#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
struct task_struct;
DECLARE_TRACE(android_vh_sched_exit,
	TP_PROTO(struct task_struct *p),
	TP_ARGS(p));
#else
#define trace_android_vh_sched_exit(p)
#endif

#endif /* _TRACE_VENDOR_HOOKS_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
