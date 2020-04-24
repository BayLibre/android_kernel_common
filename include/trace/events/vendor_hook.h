/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM vendor_hook

#if !defined(_TRACE_VENDOR_HOOKS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_VENDOR_HOOKS_H

#include <linux/tracepoint.h>

/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */

struct task_struct;
DECLARE_TRACE(android_vh_sched_exit,
	TP_PROTO(int *retp, struct task_struct *p),
	TP_ARGS(retp, p));

#endif /* _TRACE_VENDOR_HOOKS_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
