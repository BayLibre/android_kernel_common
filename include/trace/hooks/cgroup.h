/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM cgroup
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_CGROUP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_CGROUP_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

struct task_struct;
DECLARE_HOOK(android_vh_cgroup_set_task,
	TP_PROTO(int ret, struct task_struct *task),
	TP_ARGS(ret, task));

DECLARE_HOOK(android_rvh_cgroup_procs_write_start,
	TP_PROTO(char *buf, bool threadgroup, bool *locked, struct cgroup *dst_cgrp,
		 struct task_struct **task, ssize_t *ret),
	TP_ARGS(buf, threadgroup, locked, dst_cgrp, task, ret));
#endif

#include <trace/define_trace.h>
