/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM ftrace_dump

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_FTRACE_DUMP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_FTRACE_DUMP_H

#include <linux/trace_seq.h>
#include <linux/trace_events.h>

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)

DECLARE_HOOK(android_vh_ftrace_format_check,
	TP_PROTO(bool *ftrace_check),
	TP_ARGS(ftrace_check));

#else

#define trace_android_vh_ftrace_format_check(ftrace_check)

#endif

#endif /* _TRACE_HOOK_FTRACE_DUMP_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
