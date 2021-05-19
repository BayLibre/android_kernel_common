/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM mi_power
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_MI_POWER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_MI_POWER_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
struct hlist_head;
DECLARE_HOOK(android_vh_clk_debug_init,
	TP_PROTO(struct hlist_head *l),
	TP_ARGS(l));
#endif
#endif /* _TRACE_HOOK_MI_POWER_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
