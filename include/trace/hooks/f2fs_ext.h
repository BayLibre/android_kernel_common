/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM f2fs_ext

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_F2FS_EXT_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_F2FS_EXT_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
DECLARE_HOOK(android_vh_f2fs_change_extra_size,
    TP_PROTO(int* plug_size),
    TP_ARGS(plug_size));

#endif
/* This part must be outside protection */
#include <trace/define_trace.h>