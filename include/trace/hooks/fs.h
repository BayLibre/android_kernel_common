/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM fs
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_FS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_FS_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
struct file;
struct task;
DECLARE_RESTRICTED_HOOK(android_vh_exec_binprm_fail,
	TP_PROTO(struct task_struct *task, struct file *file),
	TP_ARGS(task, file), 1);
#else
#define trace_android_vh_exec_binprm_fail(task, file)
#endif
#endif /* _TRACE_HOOK_FS_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
