/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM kernel
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_KERNEL_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_KERNEL_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
struct task_struct;
struct file;
DECLARE_RESTRICTED_HOOK(android_vh_ptrace,
	TP_PROTO(struct task_struct *task, long request),
	TP_ARGS(task, request), 1);
DECLARE_RESTRICTED_HOOK(android_vh_copy_process,
	TP_PROTO(unsigned long clone_flags, struct task_struct *p, int *retval),
	TP_ARGS(clone_flags, p, retval), 1);
#else
#define trace_android_vh_ptrace(task, request)
#define trace_android_vh_copy_process(clone_flags, p, retval)
#endif
#endif /* _TRACE_HOOK_KERNEL_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
