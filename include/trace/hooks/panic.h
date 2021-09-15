/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM panic

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_PANIC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_PANIC_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_panic_prepare,
	TP_PROTO(char *buf),
	TP_ARGS(buf));

#endif /* _TRACE_HOOK_PANIC_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
