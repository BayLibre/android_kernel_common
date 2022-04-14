/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM f2fshooks
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_F2FSHOOKS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_F2FSHOOKS_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
struct file;
struct inode;
struct f2fs_sb_info;
struct f2fs_defragment;

DECLARE_RESTRICTED_HOOK(android_rvh_f2fs_ioctl_defrag,
			TP_PROTO(struct file *filp, unsigned int *cmd,
			unsigned long arg, unsigned int ioc_getversion),
			TP_ARGS(filp, cmd, arg, ioc_getversion), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_set_gc_status,
			TP_PROTO(void *unused),
			TP_ARGS(unused), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_restore_gc_status,
			TP_PROTO(void *unused),
			TP_ARGS(unused), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_set_gc_mode,
			TP_PROTO(int *gc_mode),
			TP_ARGS(gc_mode), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_get_is_idle,
			TP_PROTO(struct f2fs_sb_info *sbi, int *gc_mode, int *type),
			TP_ARGS(sbi, gc_mode, type), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_get_pages_address,
			TP_PROTO(struct f2fs_sb_info *sbi, int *type),
			TP_ARGS(sbi, type), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_func_f2fs_defragment_range,
			TP_PROTO(int(*ptr)(struct f2fs_sb_info *sbi, struct file *filp,
			struct f2fs_defragment *range)),
			TP_ARGS(ptr), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_func_mnt_want_write_file,
			TP_PROTO(int(*ptr)(struct file *file)),
			TP_ARGS(ptr), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_func_mnt_drop_write_file,
			TP_PROTO(void(*ptr)(struct file *file)),
			TP_ARGS(ptr), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_inode_flag_set,
			TP_PROTO(int *flag),
			TP_ARGS(flag), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_set_is_file,
			TP_PROTO(int *type),
			TP_ARGS(type), 1);

#endif /* _TRACE_HOOK_F2FSHOOKS_H */
/* This part must be outside protection */
#include <trace/define_trace.h>

