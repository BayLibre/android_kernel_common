/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM kprobe

#if !defined(_TRACE_HOOK_KPROBE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_KPROBE_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */

DECLARE_RESTRICTED_HOOK(android_rvh_kprobe_check,
	TP_PROTO(const void *addr, unsigned int insn[], size_t size),
	TP_ARGS(addr, insn, size), 1);

#endif /* _TRACE_HOOK_KPROBE_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
