/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM strict_rwx

#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_STRICT_RWX_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_STRICT_RWX_H
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
DECLARE_RESTRICTED_HOOK(android_rvh_mod_text_enable_ro,
	TP_PROTO(const void *text, u32 text_size, const void *init, u32 init_size),
	TP_ARGS(text, text_size, init, init_size), 1);

#endif /* _TRACE_HOOK_STRICT_RWX_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
