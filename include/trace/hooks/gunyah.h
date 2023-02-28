/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM gunyah

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_GUNYAH_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_GUNYAH_H

#include <trace/hooks/vendor_hooks.h>

/* To load PIL VMs */
DECLARE_RESTRICTED_HOOK(android_rvh_gunyah_loader_dev_ioctl,
	TP_PROTO(unsigned int cmd, unsigned long arg, long *ret),
	TP_ARGS(cmd, arg, ret), 1);

#endif /* _TRACE_HOOK_GUNYAH_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
