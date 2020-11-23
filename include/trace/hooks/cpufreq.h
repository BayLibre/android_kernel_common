/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM cpufreq

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_CPUFREQ_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_CPUFREQ_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)

DECLARE_HOOK(android_vh_show_max_freq,
	TP_PROTO(unsigned int *max_freq),
	TP_ARGS(max_freq));

DECLARE_HOOK(android_vh_freq_table_limits,
	TP_PROTO(unsigned int min_freq, unsigned int max_freq),
	TP_ARGS(min_freq, max_freq));
#else

#define trace_android_vh_show_max_freq(max_freq)
#define trace_android_vh_freq_table_limits(min_freq, max_freq)

#endif

#endif /* _TRACE_HOOK_CPUFREQ_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
