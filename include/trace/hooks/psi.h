/* SPDX-License-Identifier: GPL-2.0-only */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM psi

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_PSI_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_PSI_H

#include <trace/hooks/vendor_hooks.h>

struct psi_trigger;
DECLARE_HOOK(android_vh_psi_update_triggers,
	TP_PROTO(struct psi_trigger *t, u64 now, u64 growth),
	TP_ARGS(t, now, growth));

#endif /* _TRACE_HOOK_PSI_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
