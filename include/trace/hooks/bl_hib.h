/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM bl_hib

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_BL_HIB_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_BL_HIB_H

#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_check_hibernation_swap,
	TP_PROTO(dev_t dev, bool *hib_swap),
	TP_ARGS(dev, hib_swap));

DECLARE_HOOK(android_vh_save_cpu_resume,
	TP_PROTO(u64 *addr, u64 phys_addr),
	TP_ARGS(addr, phys_addr));

DECLARE_HOOK(android_vh_save_hib_resume_bdev,
	TP_PROTO(dev_t dev),
	TP_ARGS(hib_resume_bdev));

#endif /* _TRACE_HOOK_BL_HIB_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
