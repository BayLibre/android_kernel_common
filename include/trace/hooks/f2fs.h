/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM f2fs
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_F2FS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_F2FS_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

struct f2fs_sb_info;

DECLARE_HOOK(android_vh_f2fs_init_sbi_oem_data,
	TP_PROTO(struct f2fs_sb_info *sbi),
	TP_ARGS(sbi));

DECLARE_HOOK(android_vh_f2fs_destory_sbi_oem_data,
	TP_PROTO(struct f2fs_sb_info *sbi),
	TP_ARGS(sbi));

#endif /* _TRACE_HOOK_F2FS_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
