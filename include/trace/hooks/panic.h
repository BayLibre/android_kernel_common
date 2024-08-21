/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM panic

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_PANIC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_PANIC_H

#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_panic,
	TP_PROTO(void *unused),
	TP_ARGS(unused));

#endif /* _TRACE_HOOK_PANIC_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
