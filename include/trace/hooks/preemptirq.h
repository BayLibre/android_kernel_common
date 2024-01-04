/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM preemptirq

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks


#include <trace/hooks/vendor_hooks.h>

DECLARE_RESTRICTED_HOOK(android_rvh_preempt_disable,
	TP_PROTO(unsigned long ip, unsigned long parent_ip),
	TP_ARGS(ip, parent_ip), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_preempt_enable,
	TP_PROTO(unsigned long ip, unsigned long parent_ip),
	TP_ARGS(ip, parent_ip), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_irqs_disable,
	TP_PROTO(unsigned long ip, unsigned long parent_ip),
	TP_ARGS(ip, parent_ip), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_irqs_enable,
	TP_PROTO(unsigned long ip, unsigned long parent_ip),
	TP_ARGS(ip, parent_ip), 1);

/* This part must be outside protection */
#include <trace/define_trace.h>
