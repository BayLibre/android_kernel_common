/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM creds

#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_CREDS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_CREDS_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
struct cred;
struct task_struct;
DECLARE_HOOK(android_vh_commit_creds, TP_PROTO(const struct task_struct *task, const struct cred *new), TP_ARGS(task, new));
DECLARE_HOOK(android_vh_exit_creds, TP_PROTO(const struct task_struct *task, const struct cred *cred), TP_ARGS(task, cred));
DECLARE_HOOK(android_vh_override_creds, TP_PROTO(const struct task_struct *task, const struct cred *new), TP_ARGS(task, new));
DECLARE_HOOK(android_vh_revert_creds, TP_PROTO(const struct task_struct *task, const struct cred *old), TP_ARGS(task, old));

#else
#define trace_android_vh_commit_creds(task, new)
#define trace_android_vh_exit_creds(task, cred)
#define trace_android_vh_override_creds(task, new)
#define trace_android_vh_revert_creds(task, old)
#endif

#endif /* _TRACE_HOOK_CREDS_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
