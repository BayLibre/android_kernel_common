/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM irqmanage
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_IRQMANAGE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_IRQMANAGE_H
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */

struct irq_desc;
DECLARE_HOOK(android_vh_synchronize_irq_start,
	TP_PROTO(struct irq_desc *desc),
	TP_ARGS(desc));
DECLARE_HOOK(android_vh_synchronize_irq_finish,
	TP_PROTO(struct irq_desc *desc),
	TP_ARGS(desc));

/* macro versions of hooks are no longer required */

#endif /* _TRACE_HOOK_IRQMANAGE_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
