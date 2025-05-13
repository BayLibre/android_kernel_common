/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM probetimeout

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_PROBETIMEOUT_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_PROBETIMEOUT_H

#include <trace/hooks/vendor_hooks.h>

DECLARE_RESTRICTED_HOOK(android_rvh_dpm_prepare,
	TP_PROTO(int flag),
	TP_ARGS(flag), 1);

#endif /* _TRACE_HOOK_PROBETIMEOUT_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
