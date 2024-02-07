/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM tmpfile
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_TMPFILE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_TMPFILE_H

#include <trace/hooks/vendor_hooks.h>

struct fuse_args;
struct inode;
struct dentry;
DECLARE_HOOK(android_vh_tmpfile_secctx,
	TP_PROTO(struct fuse_args *args, u32 security_ctxlen, void *security_ctx,
		 bool *skip_ctxargset),
	TP_ARGS(args, security_ctxlen, security_ctx, skip_ctxargset));
DECLARE_HOOK(android_vh_tmpfile_create_check_inode,
	TP_PROTO(struct fuse_args *args, struct inode *inode, int *err),
	TP_ARGS(args, inode, err));
DECLARE_HOOK(android_vh_tmpfile_create,
	TP_PROTO(struct fuse_args *args, struct dentry **d, struct dentry *entry,
		 struct inode *inode, bool *skip_splice),
	TP_ARGS(args, d, entry, inode, skip_splice));
DECLARE_HOOK(android_vh_tmpfile_handle_op,
	TP_PROTO(struct inode *dir, struct dentry *entry, umode_t mode,
		 int *ret),
	TP_ARGS(dir, entry, mode, ret));
DECLARE_HOOK(android_vh_tmpfile_send_open,
	TP_PROTO(uint32_t *flags),
	TP_ARGS(flags));
#endif /* _TRACE_HOOK_TMPFILE_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
