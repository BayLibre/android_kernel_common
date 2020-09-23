/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM pm_wakeup

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_PM_WAKEUP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_PM_WAKEUP_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)

DECLARE_HOOK(android_vh_pm_irq_wakeup,
	TP_PROTO(bool *log_wakeup_reason),
	TP_ARGS(log_wakeup_reason));

#else

#define trace_android_vh_pm_irq_wakeup(log_wakeup_reason)

#endif

#endif /* _TRACE_HOOK_PM_WAKEUP_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
