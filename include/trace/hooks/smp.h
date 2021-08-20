/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM smp

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_SMP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_SMP_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

struct cpumask;
DECLARE_HOOK(android_gpcv2_raise_softirq,
	TP_PROTO(const struct cpumask *target, unsigned int ipinr),
	TP_ARGS(target, ipinr));

/* macro versions of hooks are no longer required */

#endif /* _TRACE_HOOK_SMP_H */

#include <trace/define_trace.h>
