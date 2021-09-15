/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM dss_dpm
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_DSS_DPM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_DSS_DPM_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */

struct pt_regs;

DECLARE_HOOK(android_vh_dss_do_dpm,
	TP_PROTO(struct pt_regs *regs),
	TP_ARGS(regs));

#endif /* _TRACE_HOOK_DSS_DPM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
