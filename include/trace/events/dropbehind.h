/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM dropbehind

#if !defined(_TRACE_DROPBEHIND_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_DROPBEHIND_H

#include <linux/fs.h>
#include <linux/hash.h>
#include <linux/kdev_t.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/tracepoint.h>
#include <linux/types.h>
#include <linux/uidgid.h>
#include <linux/user_namespace.h>

#define ANDROID_DROPBEHIND_OBS_READ		0x0001
#define ANDROID_DROPBEHIND_OBS_PREAD		0x0002
#define ANDROID_DROPBEHIND_OBS_READV		0x0004
#define ANDROID_DROPBEHIND_OBS_PREADV		0x0008
#define ANDROID_DROPBEHIND_OBS_DIRECT		0x0100
#define ANDROID_DROPBEHIND_OBS_NO_POS		0x0200

#define show_dropbehind_dev_ino(entry)					\
	MAJOR((entry)->dev), MINOR((entry)->dev),			\
	(unsigned long)(entry)->ino

TRACE_EVENT(android_dropbehind_open_observe,

	TP_PROTO(struct file *filp, unsigned int fd, const char *pathname),

	TP_ARGS(filp, fd, pathname),

	TP_STRUCT__entry(
		__field(u64, file_cookie)
		__field(unsigned int, fd)
		__field(dev_t, dev)
		__field(ino_t, ino)
		__field(loff_t, i_size)
		__field(pid_t, tgid)
		__field(pid_t, tid)
		__field(uid_t, uid)
		__field(unsigned int, f_flags)
		__field(unsigned int, f_mode)
		__dynamic_array(char, path, strlen(pathname) + 1)
		__string(comm, current->comm)
	),

	TP_fast_assign(
		struct inode *inode = file_inode(filp);

		__entry->file_cookie = (u64)hash_ptr(filp, 64);
		__entry->fd = fd;
		__entry->dev = inode->i_sb ? inode->i_sb->s_dev : inode->i_rdev;
		__entry->ino = inode->i_ino;
		__entry->i_size = i_size_read(inode);
		__entry->tgid = current->tgid;
		__entry->tid = current->pid;
		__entry->uid = from_kuid_munged(&init_user_ns, current_uid());
		__entry->f_flags = filp->f_flags;
		__entry->f_mode = filp->f_mode;
		memcpy(__get_dynamic_array(path), pathname, strlen(pathname) + 1);
		__assign_str(comm);
	),

	TP_printk("file_cookie=%llu fd=%u path=%s tgid=%d tid=%d uid=%u "
		  "dev=%d:%d ino=%lu i_size=%llu f_flags=0x%x f_mode=0x%x "
		  "comm=%s",
		(unsigned long long)__entry->file_cookie, __entry->fd,
		(char *)__get_dynamic_array(path), __entry->tgid, __entry->tid,
		__entry->uid, show_dropbehind_dev_ino(__entry),
		(unsigned long long)__entry->i_size, __entry->f_flags,
		__entry->f_mode, __get_str(comm))
);

TRACE_EVENT(android_dropbehind_read_observe,

	TP_PROTO(struct file *filp, unsigned int fd, loff_t pos_before,
		 size_t requested_bytes, ssize_t ret, unsigned int flags,
		 unsigned int rwf_flags),

	TP_ARGS(filp, fd, pos_before, requested_bytes, ret, flags, rwf_flags),

	TP_STRUCT__entry(
		__field(u64, file_cookie)
		__field(unsigned int, fd)
		__field(dev_t, dev)
		__field(ino_t, ino)
		__field(loff_t, i_size)
		__field(pid_t, tgid)
		__field(pid_t, tid)
		__field(uid_t, uid)
		__field(loff_t, pos_before)
		__field(size_t, requested_bytes)
		__field(ssize_t, ret)
		__field(loff_t, pos_after)
		__field(unsigned int, flags)
		__field(unsigned int, rwf_flags)
		__string(comm, current->comm)
	),

	TP_fast_assign(
		struct inode *inode = file_inode(filp);

		__entry->file_cookie = (u64)hash_ptr(filp, 64);
		__entry->fd = fd;
		__entry->dev = inode->i_sb ? inode->i_sb->s_dev : inode->i_rdev;
		__entry->ino = inode->i_ino;
		__entry->i_size = i_size_read(inode);
		__entry->tgid = current->tgid;
		__entry->tid = current->pid;
		__entry->uid = from_kuid_munged(&init_user_ns, current_uid());
		__entry->pos_before = pos_before;
		__entry->requested_bytes = requested_bytes;
		__entry->ret = ret;
		__entry->pos_after = ret > 0 ? pos_before + ret : pos_before;
		__entry->flags = flags;
		__entry->rwf_flags = rwf_flags;
		__assign_str(comm);
	),

	TP_printk("file_cookie=%llu fd=%u tgid=%d tid=%d uid=%u "
		  "dev=%d:%d ino=%lu i_size=%llu pos_before=%llu "
		  "requested_bytes=%zu ret=%zd pos_after=%llu flags=0x%x "
		  "rwf_flags=0x%x comm=%s",
		(unsigned long long)__entry->file_cookie, __entry->fd,
		__entry->tgid, __entry->tid, __entry->uid,
		show_dropbehind_dev_ino(__entry),
		(unsigned long long)__entry->i_size,
		(unsigned long long)__entry->pos_before,
		__entry->requested_bytes, __entry->ret,
		(unsigned long long)__entry->pos_after, __entry->flags,
		__entry->rwf_flags, __get_str(comm))
);

TRACE_EVENT(android_dropbehind_llseek_observe,

	TP_PROTO(struct file *filp, unsigned int fd, loff_t pos_before,
		 loff_t offset_arg, unsigned int whence, loff_t ret,
		 loff_t pos_after),

	TP_ARGS(filp, fd, pos_before, offset_arg, whence, ret, pos_after),

	TP_STRUCT__entry(
		__field(u64, file_cookie)
		__field(unsigned int, fd)
		__field(dev_t, dev)
		__field(ino_t, ino)
		__field(loff_t, i_size)
		__field(pid_t, tgid)
		__field(pid_t, tid)
		__field(uid_t, uid)
		__field(loff_t, pos_before)
		__field(loff_t, offset_arg)
		__field(unsigned int, whence)
		__field(loff_t, ret)
		__field(loff_t, pos_after)
		__string(comm, current->comm)
	),

	TP_fast_assign(
		struct inode *inode = file_inode(filp);

		__entry->file_cookie = (u64)hash_ptr(filp, 64);
		__entry->fd = fd;
		__entry->dev = inode->i_sb ? inode->i_sb->s_dev : inode->i_rdev;
		__entry->ino = inode->i_ino;
		__entry->i_size = i_size_read(inode);
		__entry->tgid = current->tgid;
		__entry->tid = current->pid;
		__entry->uid = from_kuid_munged(&init_user_ns, current_uid());
		__entry->pos_before = pos_before;
		__entry->offset_arg = offset_arg;
		__entry->whence = whence;
		__entry->ret = ret;
		__entry->pos_after = pos_after;
		__assign_str(comm);
	),

	TP_printk("file_cookie=%llu fd=%u tgid=%d tid=%d uid=%u "
		  "dev=%d:%d ino=%lu i_size=%llu pos_before=%llu "
		  "offset_arg=%llu whence=%u ret=%lld pos_after=%llu comm=%s",
		(unsigned long long)__entry->file_cookie, __entry->fd,
		__entry->tgid, __entry->tid, __entry->uid,
		show_dropbehind_dev_ino(__entry),
		(unsigned long long)__entry->i_size,
		(unsigned long long)__entry->pos_before,
		(unsigned long long)__entry->offset_arg, __entry->whence,
		(long long)__entry->ret,
		(unsigned long long)__entry->pos_after, __get_str(comm))
);

TRACE_EVENT(android_dropbehind_close_observe,

	TP_PROTO(struct file *filp, unsigned int fd, loff_t final_pos, int ret),

	TP_ARGS(filp, fd, final_pos, ret),

	TP_STRUCT__entry(
		__field(u64, file_cookie)
		__field(unsigned int, fd)
		__field(dev_t, dev)
		__field(ino_t, ino)
		__field(loff_t, i_size)
		__field(pid_t, tgid)
		__field(pid_t, tid)
		__field(uid_t, uid)
		__field(loff_t, final_pos)
		__field(int, ret)
		__string(comm, current->comm)
	),

	TP_fast_assign(
		struct inode *inode = file_inode(filp);

		__entry->file_cookie = (u64)hash_ptr(filp, 64);
		__entry->fd = fd;
		__entry->dev = inode->i_sb ? inode->i_sb->s_dev : inode->i_rdev;
		__entry->ino = inode->i_ino;
		__entry->i_size = i_size_read(inode);
		__entry->tgid = current->tgid;
		__entry->tid = current->pid;
		__entry->uid = from_kuid_munged(&init_user_ns, current_uid());
		__entry->final_pos = final_pos;
		__entry->ret = ret;
		__assign_str(comm);
	),

	TP_printk("file_cookie=%llu fd=%u tgid=%d tid=%d uid=%u "
		  "dev=%d:%d ino=%lu i_size=%llu final_pos=%llu ret=%d "
		  "comm=%s",
		(unsigned long long)__entry->file_cookie, __entry->fd,
		__entry->tgid, __entry->tid, __entry->uid,
		show_dropbehind_dev_ino(__entry),
		(unsigned long long)__entry->i_size,
		(unsigned long long)__entry->final_pos, __entry->ret,
		__get_str(comm))
);

#endif /* _TRACE_DROPBEHIND_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
