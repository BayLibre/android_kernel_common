/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM sys
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_SYS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_SYS_H
#include <trace/hooks/vendor_hooks.h>

struct task_struct;
DECLARE_HOOK(android_vh_syscall_prctl_finished,
	TP_PROTO(int option, struct task_struct *task),
	TP_ARGS(option, task));

DECLARE_RESTRICTED_HOOK(android_rvh_syscall_prctl,
	TP_PROTO(int option, unsigned long arg2, unsigned long arg3,
		unsigned long arg4, unsigned long arg5, long *error),
	TP_ARGS(option, arg2, arg3, arg4, arg5, error), 1);
#endif

#include <trace/define_trace.h>
