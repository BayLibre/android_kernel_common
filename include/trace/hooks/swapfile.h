/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM swapfile
#undef TRACE_INCLUDE_PATH

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_SWAPFILE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_SWAPFILE_H

#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_swapoff_skip,
	TP_PROTO(struct filename *pathname, bool *skip),
	TP_ARGS(pathname, skip));

#endif /* _TRACE_HOOK_SWAPFILE_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
