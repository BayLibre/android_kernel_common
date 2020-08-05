/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM dtask
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_DTASK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_DTASK_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
struct task_struct;
DECLARE_HOOK(android_vh_sched_show_task,
	TP_PROTO(struct task_struct *task),
	TP_ARGS(task));
#else
#define trace_android_vh_sched_show_task(task)
#endif

#endif /* _TRACE_HOOK_DTASK_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
