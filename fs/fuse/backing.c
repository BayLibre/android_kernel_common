// SPDX-License-Identifier: GPL-2.0
/*
 * FUSE-BPF: Filesystem in Userspace with BPF
 * Copyright (c) 2021 Google LLC
 */

#include "fuse_i.h"

#include <linux/fdtable.h>
#include <linux/filelock.h>
#include <linux/filter.h>
#include <linux/fs_stack.h>
#include <linux/splice.h>
#include <linux/namei.h>

#include "../internal.h"

#define FUSE_BPF_IOCB_MASK (IOCB_APPEND | IOCB_DSYNC | IOCB_HIPRI | IOCB_NOWAIT | IOCB_SYNC)

struct fuse_bpf_aio_req {
	struct kiocb iocb;
	refcount_t ref;
	struct kiocb *iocb_orig;
};

static struct kmem_cache *fuse_bpf_aio_request_cachep;

static void fuse_stat_to_attr(struct fuse_conn *fc, struct inode *inode,
		struct kstat *stat, struct fuse_attr *attr);

static void fuse_copyattr(struct file *dst_file, struct file *src_file)
{
	struct inode *dst = file_inode(dst_file);
	struct inode *src = file_inode(src_file);

	inode_set_mtime_to_ts(dst, inode_get_mtime(src));
	inode_set_ctime_to_ts(dst, inode_get_ctime(src));
	inode_set_atime_to_ts(dst, inode_get_atime(src));
	i_size_write(dst, i_size_read(src));
}

static void fuse_file_accessed(struct file *dst_file, struct file *src_file)
{
	struct inode *dst_inode;
	struct inode *src_inode;
	struct timespec64 dst_ctime, src_ctime, dst_mtime, src_mtime;

	if (dst_file->f_flags & O_NOATIME)
		return;

	dst_inode = file_inode(dst_file);
	src_inode = file_inode(src_file);

	dst_ctime = inode_get_ctime(dst_inode);
	src_ctime = inode_get_ctime(src_inode);
	dst_mtime = inode_get_mtime(dst_inode);
	src_mtime = inode_get_mtime(src_inode);
	if (!timespec64_equal(&dst_mtime, &src_mtime) ||
	    !timespec64_equal(&dst_ctime, &src_ctime)) {
		// Why not just call these two unconditionally?
		inode_set_mtime_to_ts(dst_inode, inode_get_mtime(src_inode));
		inode_set_ctime_to_ts(dst_inode, inode_get_ctime(src_inode));
	}

	touch_atime(&dst_file->f_path);
}

int fuse_open_backing(struct inode *inode, struct file *file, bool isdir)
{
	struct fuse_mount *fm = get_fuse_mount(inode);
	struct fuse_file *ff;
	int retval;
	int mask;
	struct fuse_dentry *fd = get_fuse_dentry(file->f_path.dentry);
	struct file *backing_file;
	uint32_t flags = file->f_flags & ~(O_CREAT | O_EXCL | O_NOCTTY);

	ff = fuse_file_alloc(fm, true);
	if (!ff)
		return -ENOMEM;

	switch (flags & O_ACCMODE) {
	case O_RDONLY:
		mask = MAY_READ;
		break;

	case O_WRONLY:
		mask = MAY_WRITE;
		break;

	case O_RDWR:
		mask = MAY_READ | MAY_WRITE;
		break;

	default:
		retval = -EINVAL;
		goto outerr;
	}

	retval = inode_permission(&nop_mnt_idmap,
				  get_fuse_inode(inode)->backing_inode, mask);
	if (retval)
		goto outerr;

	backing_file = dentry_open(&fd->backing_path, flags, current_cred());

	if (IS_ERR(backing_file)) {
		retval = PTR_ERR(backing_file);
		goto outerr;
	}

	ff->backing_file = backing_file;
	ff->nodeid = get_fuse_inode(inode)->nodeid;
	file->private_data = ff;
	return 0;

outerr:
	if (retval)
		fuse_file_free(ff);
	return retval;
}

static int fuse_open_file_backing(struct inode *inode, struct file *file)
{
	struct fuse_mount *fm = get_fuse_mount(inode);
	struct dentry *entry = file->f_path.dentry;
	struct fuse_dentry *fuse_dentry = get_fuse_dentry(entry);
	struct fuse_file *fuse_file;
	struct file *backing_file;

	fuse_file = fuse_file_alloc(fm, true);
	if (!fuse_file)
		return -ENOMEM;
	file->private_data = fuse_file;

	backing_file = dentry_open(&fuse_dentry->backing_path, file->f_flags,
				   current_cred());
	if (IS_ERR(backing_file)) {
		fuse_file_free(fuse_file);
		file->private_data = NULL;
		return PTR_ERR(backing_file);
	}
	fuse_file->backing_file = backing_file;

	return 0;
}

int fuse_create_open_backing(
		struct inode *dir, struct dentry *entry,
		struct file *file, unsigned int flags, umode_t mode)
{
	struct fuse_inode *dir_fuse_inode = get_fuse_inode(dir);
	struct fuse_dentry *fuse_entry = get_fuse_dentry(entry);
	struct fuse_dentry *dir_fuse_dentry = get_fuse_dentry(entry->d_parent);
	struct dentry *backing_dentry = NULL;
	struct inode *inode = NULL;
	struct dentry *newent;
	int err = 0;
	struct inode *d_inode = entry->d_inode;
	u64 target_nodeid = 0;

	if (!dir_fuse_inode || !dir_fuse_dentry)
		return -EIO;

	inode_lock_nested(dir_fuse_inode->backing_inode, I_MUTEX_PARENT);
	backing_dentry = lookup_one_len(entry->d_name.name,
					dir_fuse_dentry->backing_path.dentry,
					entry->d_name.len);
	inode_unlock(dir_fuse_inode->backing_inode);

	if (IS_ERR(backing_dentry))
		return PTR_ERR(backing_dentry);

	if (d_really_is_positive(backing_dentry)) {
		err = -EIO;
		goto out;
	}

	err = vfs_create(&nop_mnt_idmap, dir_fuse_inode->backing_inode,
			 backing_dentry, mode, true);
	if (err)
		goto out;

	if (fuse_entry->backing_path.dentry)
		path_put(&fuse_entry->backing_path);
	fuse_entry->backing_path = (struct path) {
		.mnt = dir_fuse_dentry->backing_path.mnt,
		.dentry = backing_dentry,
	};
	path_get(&fuse_entry->backing_path);

	if (d_inode)
		target_nodeid = get_fuse_inode(d_inode)->nodeid;

	inode = fuse_iget_backing(dir->i_sb, target_nodeid,
			fuse_entry->backing_path.dentry->d_inode);
	if (!inode) {
		err = -EIO;
		goto out;
	}

	get_fuse_inode(inode)->android_magic = fuse_entry->android_magic;

	newent = d_splice_alias(inode, entry);
	if (IS_ERR(newent)) {
		err = PTR_ERR(newent);
		goto out;
	}

	inode = NULL;
	entry = newent ? newent : entry;
	err = finish_open(file, entry, fuse_open_file_backing);

out:
	iput(inode);
	dput(backing_dentry);
	return err;
}

int fuse_flush_backing(struct file *file, fl_owner_t id)
{
	struct fuse_file *fuse_file = file->private_data;
	struct file *backing_file = fuse_file->backing_file;

	if (backing_file->f_op->flush)
		return backing_file->f_op->flush(backing_file, id);
	return 0;
}

int fuse_lseek_backing(struct file *file, loff_t offset, int whence)
{
	struct fuse_file *fuse_file = file->private_data;
	struct file *backing_file = fuse_file->backing_file;
	loff_t ret;

	if (offset == 0) {
		if (whence == SEEK_CUR)
			return file->f_pos;

		if (whence == SEEK_SET)
			return vfs_setpos(file, 0, 0);
	}

	inode_lock(file->f_inode);
	backing_file->f_pos = file->f_pos;
	ret = vfs_llseek(backing_file, offset, whence);
	inode_unlock(file->f_inode);
	return ret;
}

int fuse_copy_file_range_backing(struct file *file_in, loff_t pos_in,
	struct file *file_out, loff_t pos_out, size_t len, unsigned int flags)
{
	struct fuse_file *fuse_file_in = file_in->private_data;
	struct file *backing_file_in = fuse_file_in->backing_file;
	struct fuse_file *fuse_file_out = file_out->private_data;
	struct file *backing_file_out = fuse_file_out->backing_file;

	if (backing_file_out)
		return vfs_copy_file_range(backing_file_in, pos_in, backing_file_out,
			pos_out, len, flags);
	else
		return vfs_copy_file_range(file_in, pos_in, file_out, pos_out, len,
					       flags);
}

int fuse_fsync_backing(struct file *file, int datasync)
{
	struct fuse_file *fuse_file = file->private_data;
	struct file *backing_file = fuse_file->backing_file;

	return vfs_fsync(backing_file, datasync);
}

static inline void fuse_bpf_aio_put(struct fuse_bpf_aio_req *aio_req)
{
	if (refcount_dec_and_test(&aio_req->ref))
		kmem_cache_free(fuse_bpf_aio_request_cachep, aio_req);
}

static void fuse_bpf_aio_cleanup_handler(struct fuse_bpf_aio_req *aio_req)
{
	struct kiocb *iocb = &aio_req->iocb;
	struct kiocb *iocb_orig = aio_req->iocb_orig;

	if (iocb->ki_flags & IOCB_WRITE) {
		kiocb_end_write(iocb);
		fuse_copyattr(iocb_orig->ki_filp, iocb->ki_filp);
	}
	iocb_orig->ki_pos = iocb->ki_pos;
	fuse_bpf_aio_put(aio_req);
}

static void fuse_bpf_aio_rw_complete(struct kiocb *iocb, long res)
{
	struct fuse_bpf_aio_req *aio_req =
		container_of(iocb, struct fuse_bpf_aio_req, iocb);
	struct kiocb *iocb_orig = aio_req->iocb_orig;

	fuse_bpf_aio_cleanup_handler(aio_req);
	iocb_orig->ki_complete(iocb_orig, res);
}

int fuse_file_read_iter_backing(struct kiocb *iocb, struct iov_iter *to)
{
	struct file *file = iocb->ki_filp;
	struct fuse_file *ff = file->private_data;
	ssize_t ret;

	if (!iov_iter_count(to))
		return 0;

	if ((iocb->ki_flags & IOCB_DIRECT) &&
	    (!ff->backing_file->f_mapping->a_ops ||
	     !ff->backing_file->f_mapping->a_ops->direct_IO))
		return -EINVAL;

	/* TODO This just plain ignores any change to fuse_read_in */
	if (is_sync_kiocb(iocb)) {
		ret = vfs_iter_read(ff->backing_file, to, &iocb->ki_pos,
				iocb->ki_flags & FUSE_BPF_IOCB_MASK);
	} else {
		struct fuse_bpf_aio_req *aio_req;

		ret = -ENOMEM;
		aio_req = kmem_cache_zalloc(fuse_bpf_aio_request_cachep, GFP_KERNEL);
		if (!aio_req)
			goto out;

		aio_req->iocb_orig = iocb;
		kiocb_clone(&aio_req->iocb, iocb, ff->backing_file);
		aio_req->iocb.ki_complete = fuse_bpf_aio_rw_complete;
		refcount_set(&aio_req->ref, 2);
		ret = vfs_iocb_iter_read(ff->backing_file, &aio_req->iocb, to);
		fuse_bpf_aio_put(aio_req);
		if (ret != -EIOCBQUEUED)
			fuse_bpf_aio_cleanup_handler(aio_req);
	}

out:
	fuse_file_accessed(file, ff->backing_file);

	return ret;
}

int fuse_file_write_iter_backing(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct fuse_file *ff = file->private_data;
	ssize_t ret;

	if (!iov_iter_count(from))
		return 0;

	inode_lock(file_inode(file));

	fuse_copyattr(file, ff->backing_file);

	if (is_sync_kiocb(iocb)) {
		ret = vfs_iter_write(ff->backing_file, from, &iocb->ki_pos,
					   iocb->ki_flags & FUSE_BPF_IOCB_MASK);

		/* Must reflect change in size of backing file to upper file */
		if (ret > 0)
			fuse_copyattr(file, ff->backing_file);
	} else {
		struct fuse_bpf_aio_req *aio_req;

		ret = -ENOMEM;
		aio_req = kmem_cache_zalloc(fuse_bpf_aio_request_cachep, GFP_KERNEL);
		if (!aio_req)
			goto out;

		aio_req->iocb_orig = iocb;
		kiocb_clone(&aio_req->iocb, iocb, ff->backing_file);
		aio_req->iocb.ki_complete = fuse_bpf_aio_rw_complete;
		refcount_set(&aio_req->ref, 2);
		ret = vfs_iocb_iter_write(ff->backing_file, &aio_req->iocb, from);
		fuse_bpf_aio_put(aio_req);
		if (ret != -EIOCBQUEUED)
			fuse_bpf_aio_cleanup_handler(aio_req);
	}

out:
	inode_unlock(file_inode(file));
	return ret;
}

ssize_t fuse_splice_read_backing(struct file *in, loff_t *ppos,
		struct pipe_inode_info *pipe, size_t len, unsigned long flags)
{
	struct fuse_file *ff = in->private_data;
	ssize_t ret;

	ret = vfs_splice_read(ff->backing_file, ppos, pipe, len, flags);
	fuse_file_accessed(in, ff->backing_file);

	return ret;
}

ssize_t fuse_splice_write_backing(struct pipe_inode_info *pipe,
		struct file *out, loff_t *ppos, size_t len, unsigned long flags)
{
	ssize_t ret;
	struct fuse_file *ff = out->private_data;

	inode_lock(file_inode(out));
	file_start_write(ff->backing_file);
	ret = iter_file_splice_write(pipe, ff->backing_file, ppos, len, flags);
	file_end_write(ff->backing_file);
	if (ret > 0)
		fuse_copyattr(out, ff->backing_file);
	inode_unlock(file_inode(out));
	return ret;
}

long fuse_backing_ioctl(struct file *file, unsigned int command, unsigned long arg, int flags)
{
	struct fuse_file *ff = file->private_data;
	long ret;

	if (flags & FUSE_IOCTL_COMPAT)
		ret = -ENOTTY;
	else
		ret = vfs_ioctl(ff->backing_file, command, arg);

	return ret;
}

int fuse_file_flock_backing(struct file *file, int cmd, struct file_lock *fl)
{
	struct fuse_file *ff = file->private_data;
	struct file *backing_file = ff->backing_file;
	int error;

	fl->c.flc_file = backing_file;
	if (backing_file->f_op->flock)
		error = backing_file->f_op->flock(backing_file, cmd, fl);
	else
		error = locks_lock_file_wait(backing_file, fl);
	return error;
}

ssize_t fuse_backing_mmap(struct file *file, struct vm_area_struct *vma)
{
	int ret;
	struct fuse_file *ff = file->private_data;
	struct inode *fuse_inode = file_inode(file);
	struct file *backing_file = ff->backing_file;
	struct inode *backing_inode = file_inode(backing_file);
	struct timespec64 fuse_inode_ctime, backing_inode_ctime;
	struct timespec64 fuse_inode_mtime, backing_inode_mtime;

	if (!backing_file->f_op->mmap)
		return -ENODEV;

	if (WARN_ON(file != vma->vm_file))
		return -EIO;

	vma->vm_file = get_file(backing_file);

	ret = call_mmap(vma->vm_file, vma);

	if (ret)
		fput(backing_file);
	else
		fput(file);

	if (file->f_flags & O_NOATIME)
		return ret;

	fuse_inode_ctime = inode_get_ctime(fuse_inode);
	backing_inode_ctime = inode_get_ctime(backing_inode);
	fuse_inode_mtime = inode_get_mtime(fuse_inode);
	backing_inode_mtime = inode_get_mtime(backing_inode);
	if ((!timespec64_equal(&fuse_inode_mtime, &backing_inode_mtime) ||
	     !timespec64_equal(&fuse_inode_ctime, &backing_inode_ctime))) {
		inode_set_mtime_to_ts(fuse_inode, backing_inode_mtime);
		inode_set_ctime_to_ts(fuse_inode, backing_inode_ctime);
	}
	touch_atime(&file->f_path);

	return ret;
}

int fuse_file_fallocate_backing(struct file *file, int mode, loff_t offset,
	loff_t length)
{
	struct fuse_file *ff = file->private_data;

	return vfs_fallocate(ff->backing_file, mode, offset, length);
}

/*******************************************************************************
 * Directory operations after here                                             *
 ******************************************************************************/

int fuse_lookup_initialize(struct fuse_bpf_args *fa, struct fuse_lookup_io *fli,
	       struct inode *dir, struct dentry *entry, unsigned int flags)
{
	*fa = (struct fuse_bpf_args) {
		.nodeid = get_fuse_inode(dir)->nodeid,
		.opcode = FUSE_LOOKUP,
		.in_numargs = 1,
		.out_numargs = 2,
		.flags = FUSE_BPF_OUT_ARGVAR,
		.in_args[0] = (struct fuse_bpf_in_arg) {
			.size = entry->d_name.len + 1,
			.value = entry->d_name.name,
		},
		.out_args[0] = (struct fuse_bpf_arg) {
			.size = sizeof(fli->feo),
			.value = &fli->feo,
		},
		.out_args[1] = (struct fuse_bpf_arg) {
			.size = sizeof(fli->feb.out),
			.value = &fli->feb.out,
		},
	};

	printk("Lookup: %llx %s", fa->nodeid, entry->d_name.name);

	return 0;
}

int fuse_lookup_backing(struct fuse_bpf_args *fa, struct inode *dir,
			  struct dentry *entry, unsigned int flags)
{
	struct fuse_dentry *fuse_entry = get_fuse_dentry(entry);
	struct fuse_dentry *dir_fuse_entry = get_fuse_dentry(entry->d_parent);
	struct dentry *dir_backing_entry = dir_fuse_entry->backing_path.dentry;
	struct inode *dir_backing_inode = dir_backing_entry->d_inode;
	struct dentry *backing_entry;
	struct fuse_entry_out *feo = (void *)fa->out_args[0].value;
	struct kstat stat;
	int err;

	inode_lock_nested(dir_backing_inode, I_MUTEX_PARENT);
	backing_entry = lookup_one_len(entry->d_name.name, dir_backing_entry,
					strlen(entry->d_name.name));
	inode_unlock(dir_backing_inode);

	if (IS_ERR(backing_entry))
		return PTR_ERR(backing_entry);

	fuse_entry->backing_path = (struct path) {
		.dentry = backing_entry,
		.mnt = mntget(dir_fuse_entry->backing_path.mnt),
	};

	if (d_is_negative(backing_entry)) {
		fa->error_in = -ENOENT;
		return 0;
	}

	err = follow_down(&fuse_entry->backing_path, 0);
	if (err)
		goto err_out;

	err = vfs_getattr(&fuse_entry->backing_path, &stat,
				  STATX_BASIC_STATS, 0);
	if (err)
		goto err_out;

	fuse_stat_to_attr(get_fuse_conn(dir),
			  backing_entry->d_inode, &stat, &feo->attr);
	return 0;

err_out:
	path_put(&fuse_entry->backing_path);
	fuse_entry->backing_path = (struct path) { };
	return err;
}

int fuse_handle_backing(struct fuse_entry_bpf *feb, struct inode **backing_inode,
			struct path *backing_path)
{
	switch (feb->out.backing_action) {
	case FUSE_ACTION_KEEP:
		/* backing inode/path are added in fuse_lookup_backing */
		break;

	case FUSE_ACTION_REMOVE:
		iput(*backing_inode);
		*backing_inode = NULL;
		path_put(backing_path);
		*backing_path = (struct path) { };
		break;

	case FUSE_ACTION_REPLACE: {
		struct file *backing_file = feb->backing_file;

		if (!backing_file)
			return -EINVAL;
		if (IS_ERR(backing_file))
			return PTR_ERR(backing_file);

		if (backing_inode)
			iput(*backing_inode);
		*backing_inode = backing_file->f_inode;
		ihold(*backing_inode);

		path_put(backing_path);
		*backing_path = backing_file->f_path;
		path_get(backing_path);
		break;
	}

	default:
		return -EINVAL;
	}

	return 0;
}

void fuse_handle_bpf_prog(struct fuse_entry_bpf *feb, struct inode *parent,
	bool *android_magic)
{
	switch (feb->out.bpf_action) {
	case FUSE_ACTION_KEEP: {
		/* Parent isn't presented, but we want to keep
		 * Don't touch bpf program at all in this case
		 */
		if (!parent)
			break;

		*android_magic = get_fuse_inode(parent)->android_magic;
		break;
	}

	case FUSE_ACTION_REMOVE:
		*android_magic = false;
		break;

	case FUSE_ACTION_REPLACE:
		*android_magic = true;
		break;

	default:
		WARN_ON(true);
		break;
	}
}

struct dentry *fuse_lookup_finalize(struct fuse_bpf_args *fa, struct inode *dir,
			   struct dentry *entry, unsigned int flags)
{
	struct fuse_dentry *fuse_entry;
	struct dentry *backing_entry;
	struct inode *inode = NULL, *backing_inode;
	struct inode *entry_inode = entry->d_inode;
	struct fuse_entry_out *feo = fa->out_args[0].value;
	struct fuse_entry_bpf_out *febo = fa->out_args[1].value;
	struct fuse_entry_bpf *feb = container_of(febo, struct fuse_entry_bpf,
						  out);
	int error = -1;
	u64 target_nodeid = 0;
	struct dentry *ret = NULL;

	fuse_entry = get_fuse_dentry(entry);
	if (!fuse_entry) {
		ret = ERR_PTR(-EIO);
		goto out;
	}

	backing_entry = fuse_entry->backing_path.dentry;
	if (!backing_entry) {
		ret = ERR_PTR(-ENOENT);
		goto out;
	}

	if (entry_inode)
		target_nodeid = get_fuse_inode(entry_inode)->nodeid;

	backing_inode = backing_entry->d_inode;
	if (backing_inode)
		inode = fuse_iget_backing(dir->i_sb, target_nodeid,
					  backing_inode);

	if (inode) {
		error = fuse_handle_backing(feb,
					&get_fuse_inode(inode)->backing_inode,
					&fuse_entry->backing_path);
		if (error) {
			ret = ERR_PTR(error);
			goto out;
		}

		fuse_handle_bpf_prog(feb, dir,
			&get_fuse_inode(inode)->android_magic);
		get_fuse_inode(inode)->nodeid = feo->nodeid;
		ret = d_splice_alias(inode, entry);
		if (!IS_ERR(ret))
			inode = NULL;

	} else {
		fuse_handle_bpf_prog(feb, dir, &fuse_entry->android_magic);
	}
out:
	iput(inode);
	if (feb->backing_file)
		fput(feb->backing_file);
	return ret;
}

int fuse_revalidate_backing(struct dentry *entry, unsigned int flags)
{
	struct fuse_dentry *fuse_dentry = get_fuse_dentry(entry);
	struct dentry *backing_entry = fuse_dentry->backing_path.dentry;

	spin_lock(&backing_entry->d_lock);
	if (d_unhashed(backing_entry)) {
		spin_unlock(&backing_entry->d_lock);
			return 0;
	}
	spin_unlock(&backing_entry->d_lock);

	if (unlikely(backing_entry->d_flags & DCACHE_OP_REVALIDATE))
		return backing_entry->d_op->d_revalidate(backing_entry, flags);
	return 1;
}

int fuse_mknod_backing(struct inode *dir, struct dentry *entry, umode_t mode,
	dev_t rdev)
{
	int err = 0;
	struct fuse_inode *fuse_inode = get_fuse_inode(dir);
	struct inode *backing_inode = fuse_inode->backing_inode;
	struct path backing_path = {};
	struct inode *inode = NULL;

	get_fuse_backing_path(entry, &backing_path);
	if (!backing_path.dentry)
		return -EBADF;

	inode_lock_nested(backing_inode, I_MUTEX_PARENT);
	if (!IS_POSIXACL(backing_inode))
		mode &= ~current_umask();
	err = vfs_mknod(&nop_mnt_idmap, backing_inode, backing_path.dentry,
		mode, rdev);
	inode_unlock(backing_inode);
	if (err)
		goto out;
	if (d_really_is_negative(backing_path.dentry) ||
		unlikely(d_unhashed(backing_path.dentry))) {
		err = -EINVAL;
		/**
		 * TODO: overlayfs responds to this situation with a
		 * lookupOneLen. Should we do that too?
		 */
		goto out;
	}
	inode = fuse_iget_backing(dir->i_sb, fuse_inode->nodeid, backing_inode);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out;
	}
	d_instantiate(entry, inode);
out:
	path_put(&backing_path);
	return err;
}

int fuse_mkdir_backing(struct inode *dir_inode, struct dentry *entry,
	umode_t mode)
{
	int err = 0;
	struct fuse_inode *dir_fuse_inode = get_fuse_inode(dir_inode);
	struct inode *dir_backing_inode = dir_fuse_inode->backing_inode;
	struct path backing_path = {};
	struct inode *inode = NULL;

	get_fuse_backing_path(entry, &backing_path);
	if (!backing_path.dentry)
		return -EBADF;

	inode_lock_nested(dir_backing_inode, I_MUTEX_PARENT);
	if (!IS_POSIXACL(dir_backing_inode))
		mode &= ~current_umask();
	err = vfs_mkdir(&nop_mnt_idmap, dir_backing_inode, backing_path.dentry, mode);
	if (err)
		goto out;
	if (d_really_is_negative(backing_path.dentry) ||
		unlikely(d_unhashed(backing_path.dentry))) {
		struct dentry *d = lookup_one_len(entry->d_name.name,
					backing_path.dentry->d_parent,
					entry->d_name.len);

		if (IS_ERR(d)) {
			err = PTR_ERR(d);
			goto out;
		}
		dput(backing_path.dentry);
		backing_path.dentry = d;
	}
	inode = fuse_iget_backing(dir_inode->i_sb, 0,
				  backing_path.dentry->d_inode);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out;
	}
	d_instantiate(entry, inode);
	get_fuse_inode(inode)->android_magic =
			get_fuse_dentry(entry)->android_magic;
out:
	inode_unlock(dir_backing_inode);
	path_put(&backing_path);
	return err;
}

int fuse_rmdir_backing(struct inode *dir, struct dentry *entry)
{
	int err = 0;
	struct path backing_path = {};
	struct dentry *backing_parent_dentry;
	struct inode *backing_inode;

	get_fuse_backing_path(entry, &backing_path);
	if (!backing_path.dentry)
		return -EBADF;

	/* TODO Not sure if we should reverify like overlayfs, or get inode from d_parent */
	backing_parent_dentry = dget_parent(backing_path.dentry);
	backing_inode = d_inode(backing_parent_dentry);

	inode_lock_nested(backing_inode, I_MUTEX_PARENT);
	err = vfs_rmdir(&nop_mnt_idmap, backing_inode, backing_path.dentry);
	inode_unlock(backing_inode);

	dput(backing_parent_dentry);
	if (!err)
		d_drop(entry);
	path_put(&backing_path);
	return err;
}

int fuse_rename_backing(struct inode *olddir, struct dentry *oldent,
			struct inode *newdir, struct dentry *newent,
			unsigned int flags)
{
	int err = 0;
	struct path old_backing_path;
	struct path new_backing_path;
	struct dentry *old_backing_dir_dentry;
	struct dentry *old_backing_dentry;
	struct dentry *new_backing_dir_dentry;
	struct dentry *new_backing_dentry;
	struct dentry *trap = NULL;
	struct inode *target_inode;
	struct renamedata rd;

	//TODO Actually deal with changing anything that isn't a flag
	get_fuse_backing_path(oldent, &old_backing_path);
	if (!old_backing_path.dentry)
		return -EBADF;
	get_fuse_backing_path(newent, &new_backing_path);
	if (!new_backing_path.dentry) {
		/*
		 * TODO A file being moved from a backing path to another
		 * backing path which is not yet instrumented with FUSE-BPF.
		 * This may be slow and should be substituted with something
		 * more clever.
		 */
		err = -EXDEV;
		goto put_old_path;
	}
	if (new_backing_path.mnt != old_backing_path.mnt) {
		err = -EXDEV;
		goto put_new_path;
	}
	old_backing_dentry = old_backing_path.dentry;
	new_backing_dentry = new_backing_path.dentry;
	old_backing_dir_dentry = dget_parent(old_backing_dentry);
	new_backing_dir_dentry = dget_parent(new_backing_dentry);
	target_inode = d_inode(newent);

	trap = lock_rename(old_backing_dir_dentry, new_backing_dir_dentry);
	if (trap == old_backing_dentry) {
		err = -EINVAL;
		goto put_parents;
	}
	if (trap == new_backing_dentry) {
		err = -ENOTEMPTY;
		goto put_parents;
	}
	rd = (struct renamedata) {
		.old_mnt_idmap = &nop_mnt_idmap,
		.old_dir = d_inode(old_backing_dir_dentry),
		.old_dentry = old_backing_dentry,
		.new_mnt_idmap = &nop_mnt_idmap,
		.new_dir = d_inode(new_backing_dir_dentry),
		.new_dentry = new_backing_dentry,
		.flags = flags,
	};
	err = vfs_rename(&rd);
	if (err)
		goto unlock;
	if (target_inode)
		fsstack_copy_attr_all(target_inode,
				get_fuse_inode(target_inode)->backing_inode);
	fsstack_copy_attr_all(d_inode(oldent), d_inode(old_backing_dentry));
unlock:
	unlock_rename(old_backing_dir_dentry, new_backing_dir_dentry);
put_parents:
	dput(new_backing_dir_dentry);
	dput(old_backing_dir_dentry);
put_new_path:
	path_put(&new_backing_path);
put_old_path:
	path_put(&old_backing_path);
	return err;
}

int fuse_unlink_backing(struct inode *dir, struct dentry *entry)
{
	int err = 0;
	struct path backing_path = {};
	struct dentry *backing_parent_dentry;
	struct inode *backing_inode;

	get_fuse_backing_path(entry, &backing_path);
	if (!backing_path.dentry)
		return -EBADF;

	/* TODO Not sure if we should reverify like overlayfs, or get inode from d_parent */
	backing_parent_dentry = dget_parent(backing_path.dentry);
	backing_inode = d_inode(backing_parent_dentry);

	inode_lock_nested(backing_inode, I_MUTEX_PARENT);
	err = vfs_unlink(&nop_mnt_idmap, backing_inode, backing_path.dentry, NULL);
	inode_unlock(backing_inode);

	dput(backing_parent_dentry);
	if (!err)
		d_drop(entry);
	path_put(&backing_path);
	return err;
}

int fuse_link_backing(struct dentry *entry, struct inode *dir,
	struct dentry *newent)
{
	int err = 0;
	struct path backing_old_path = {};
	struct path backing_new_path = {};
	struct dentry *backing_dir_dentry;
	struct inode *fuse_new_inode = NULL;
	struct fuse_inode *fuse_dir_inode = get_fuse_inode(dir);
	struct inode *backing_dir_inode = fuse_dir_inode->backing_inode;

	get_fuse_backing_path(entry, &backing_old_path);
	if (!backing_old_path.dentry)
		return -EBADF;

	get_fuse_backing_path(newent, &backing_new_path);
	if (!backing_new_path.dentry) {
		err = -EBADF;
		goto err_dst_path;
	}

	backing_dir_dentry = dget_parent(backing_new_path.dentry);
	backing_dir_inode = d_inode(backing_dir_dentry);

	inode_lock_nested(backing_dir_inode, I_MUTEX_PARENT);
	err = vfs_link(backing_old_path.dentry, &nop_mnt_idmap,
		       backing_dir_inode, backing_new_path.dentry, NULL);
	inode_unlock(backing_dir_inode);
	if (err)
		goto out;

	if (d_really_is_negative(backing_new_path.dentry) ||
	    unlikely(d_unhashed(backing_new_path.dentry))) {
		err = -EINVAL;
		/**
		 * TODO: overlayfs responds to this situation with a
		 * lookupOneLen. Should we do that too?
		 */
		goto out;
	}

	fuse_new_inode = fuse_iget_backing(dir->i_sb, fuse_dir_inode->nodeid, backing_dir_inode);
	if (IS_ERR(fuse_new_inode)) {
		err = PTR_ERR(fuse_new_inode);
		goto out;
	}
	d_instantiate(newent, fuse_new_inode);

out:
	dput(backing_dir_dentry);
	path_put(&backing_new_path);
err_dst_path:
	path_put(&backing_old_path);
	return err;
}

static void fuse_stat_to_attr(struct fuse_conn *fc, struct inode *inode,
		struct kstat *stat, struct fuse_attr *attr)
{
	unsigned int blkbits;

	/* see the comment in fuse_change_attributes() */
	if (fc->writeback_cache && S_ISREG(inode->i_mode)) {
		stat->size = i_size_read(inode);
		stat->mtime.tv_sec = inode_get_mtime_sec(inode);;
		stat->mtime.tv_nsec = inode_get_mtime_nsec(inode);
		stat->ctime.tv_sec = inode_get_ctime_sec(inode);
		stat->ctime.tv_nsec = inode_get_ctime_nsec(inode);
	}

	attr->ino = stat->ino;
	attr->mode = (inode->i_mode & S_IFMT) | (stat->mode & 07777);
	attr->nlink = stat->nlink;
	attr->uid = from_kuid(fc->user_ns, stat->uid);
	attr->gid = from_kgid(fc->user_ns, stat->gid);
	attr->atime = stat->atime.tv_sec;
	attr->atimensec = stat->atime.tv_nsec;
	attr->mtime = stat->mtime.tv_sec;
	attr->mtimensec = stat->mtime.tv_nsec;
	attr->ctime = stat->ctime.tv_sec;
	attr->ctimensec = stat->ctime.tv_nsec;
	attr->size = stat->size;
	attr->blocks = stat->blocks;

	if (stat->blksize != 0)
		blkbits = ilog2(stat->blksize);
	else
		blkbits = inode->i_sb->s_blocksize_bits;

	attr->blksize = 1 << blkbits;
}

int fuse_getattr_backing(const struct dentry *entry, struct kstat *stat,
		u32 request_mask, unsigned int flags)
{
	struct inode *inode = entry->d_inode;
	struct path *backing_path = &get_fuse_dentry(entry)->backing_path;
	struct inode *backing_inode = backing_path->dentry->d_inode;
	struct kstat tmp;
	struct fuse_attr_out fao = {0};
	int err;
	u64 attr_version;

	if (!stat)
		stat = &tmp;

	if (flags & AT_GETATTR_NOSEC)
		err = vfs_getattr_nosec(backing_path, stat, request_mask, flags);
	else
		err = vfs_getattr(backing_path, stat, request_mask, flags);

	if (err)
		return err;

	fuse_stat_to_attr(get_fuse_conn(inode), backing_inode, stat, &fao.attr);
	attr_version = fuse_get_attr_version(get_fuse_mount(inode)->fc);
	return finalize_attr(inode, &fao, attr_version, stat);
}

int fuse_setattr_backing(struct dentry *dentry, struct iattr *attr,
	struct file *file)
{
	struct path *backing_path = &get_fuse_dentry(dentry)->backing_path;
	int res;

	inode_lock(d_inode(backing_path->dentry));
	res = notify_change(&nop_mnt_idmap, backing_path->dentry, attr, NULL);
	inode_unlock(d_inode(backing_path->dentry));

	if (res == 0 && (attr->ia_valid & ATTR_SIZE))
		i_size_write(dentry->d_inode, attr->ia_size);
	return res;
}

const char *fuse_get_link_backing(struct inode *inode, struct dentry *dentry,
		struct delayed_call *callback)
{
	struct path backing_path;
	const char *out;

	if (!dentry)
		return ERR_PTR(-ECHILD);

	get_fuse_backing_path(dentry, &backing_path);
	if (!backing_path.dentry)
		return ERR_PTR(-ECHILD);

	out = vfs_get_link(backing_path.dentry, callback);
	path_put(&backing_path);
	return out;
}

int fuse_symlink_backing(struct inode *dir, struct dentry *entry,
	const char *link, int len)
{
	int err = 0;
	struct fuse_inode *fuse_inode = get_fuse_inode(dir);
	struct inode *backing_inode = fuse_inode->backing_inode;
	struct path backing_path = {};
	struct inode *inode = NULL;

	get_fuse_backing_path(entry, &backing_path);
	if (!backing_path.dentry)
		return -EBADF;

	inode_lock_nested(backing_inode, I_MUTEX_PARENT);
	err = vfs_symlink(&nop_mnt_idmap, backing_inode, backing_path.dentry, link);
	inode_unlock(backing_inode);
	if (err)
		goto out;
	if (d_really_is_negative(backing_path.dentry) ||
		unlikely(d_unhashed(backing_path.dentry))) {
		err = -EINVAL;
		/**
		 * TODO: overlayfs responds to this situation with a
		 * lookupOneLen. Should we do that too?
		 */
		goto out;
	}
	inode = fuse_iget_backing(dir->i_sb, fuse_inode->nodeid, backing_inode);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out;
	}
	d_instantiate(entry, inode);
out:
	path_put(&backing_path);
	return err;
}

int fuse_readdir_initialize(struct fuse_bpf_args *fa, struct fuse_read_io *frio,
			    struct file *file, struct dir_context *ctx,
			    bool *force_again, bool *allow_force, bool is_continued)
{
	struct fuse_file *ff = file->private_data;
	u8 *page = (u8 *)__get_free_page(GFP_KERNEL);

	if (!page)
		return -ENOMEM;

	*fa = (struct fuse_bpf_args) {
		.nodeid = ff->nodeid,
		.opcode = FUSE_READDIR,
		.in_numargs = 1,
		.flags = FUSE_BPF_OUT_ARGVAR,
		.out_numargs = 2,
		.in_args[0] = (struct fuse_bpf_in_arg) {
			.size = sizeof(frio->fri),
			.value = &frio->fri,
		},
		.out_args[0] = (struct fuse_bpf_arg) {
			.size = sizeof(frio->fro),
			.value = &frio->fro,
		},
		.out_args[1] = (struct fuse_bpf_arg) {
			.size = PAGE_SIZE,
			.value = page,
		},
	};

	frio->fri = (struct fuse_read_in) {
		.fh = ff->fh,
		.offset = ctx->pos,
		.size = PAGE_SIZE,
	};
	frio->fro = (struct fuse_read_out) {
		.again = 0,
		.offset = 0,
	};
	*force_again = false;
	*allow_force = true;
	return 0;
}

struct extfuse_ctx {
	struct dir_context ctx;
	u8 *addr;
	size_t offset;
};

static bool filldir(struct dir_context *ctx, const char *name, int namelen,
				   loff_t offset, u64 ino, unsigned int d_type)
{
	struct extfuse_ctx *ec = container_of(ctx, struct extfuse_ctx, ctx);
	struct fuse_dirent *fd = (struct fuse_dirent *) (ec->addr + ec->offset);

	if (ec->offset + sizeof(struct fuse_dirent) + namelen > PAGE_SIZE)
		return false;

	*fd = (struct fuse_dirent) {
		.ino = ino,
		.off = offset,
		.namelen = namelen,
		.type = d_type,
	};

	memcpy(fd->name, name, namelen);
	ec->offset += FUSE_DIRENT_SIZE(fd);

	return true;
}

static int parse_dirfile(char *buf, size_t nbytes, struct dir_context *ctx)
{
	while (nbytes >= FUSE_NAME_OFFSET) {
		struct fuse_dirent *dirent = (struct fuse_dirent *) buf;
		size_t reclen = FUSE_DIRENT_SIZE(dirent);

		if (!dirent->namelen || dirent->namelen > FUSE_NAME_MAX)
			return -EIO;
		if (reclen > nbytes)
			break;
		if (memchr(dirent->name, '/', dirent->namelen) != NULL)
			return -EIO;

		ctx->pos = dirent->off;
		if (!dir_emit(ctx, dirent->name, dirent->namelen, dirent->ino,
				dirent->type))
			break;

		buf += reclen;
		nbytes -= reclen;
	}

	return 0;
}


int fuse_readdir_backing(struct fuse_bpf_args *fa,
			 struct file *file, struct dir_context *ctx,
			 bool *force_again, bool *allow_force, bool is_continued)
{
	struct fuse_file *ff = file->private_data;
	struct file *backing_dir = ff->backing_file;
	struct fuse_read_out *fro = fa->out_args[0].value;
	struct extfuse_ctx ec;
	int err;

	ec = (struct extfuse_ctx) {
		.ctx.actor = filldir,
		.ctx.pos = ctx->pos,
		.addr = fa->out_args[1].value,
	};

	if (!ec.addr)
		return -ENOMEM;

	if (!is_continued)
		backing_dir->f_pos = file->f_pos;

	err = iterate_dir(backing_dir, &ec.ctx);
	if (ec.offset == 0)
		*allow_force = false;
	fa->out_args[1].size = ec.offset;

	fro->offset = ec.ctx.pos;
	fro->again = false;
	return err;
}

void *fuse_readdir_finalize(struct fuse_bpf_args *fa,
			    struct file *file, struct dir_context *ctx,
			    bool *force_again, bool *allow_force, bool is_continued)
{
	struct fuse_read_out *fro = fa->out_args[0].value;
	struct fuse_file *ff = file->private_data;
	struct file *backing_dir = ff->backing_file;
	int err = 0;

	err = parse_dirfile(fa->out_args[1].value, fa->out_args[1].size, ctx);
	*force_again = !!fro->again;
	if (*force_again && !*allow_force)
		err = -EINVAL;

	ctx->pos = fro->offset;
	backing_dir->f_pos = fro->offset;

	free_page((unsigned long) fa->out_args[1].value);
	return ERR_PTR(err);
}

int fuse_access_backing(struct inode *inode, int mask)
{
	return inode_permission(&nop_mnt_idmap,
		get_fuse_inode(inode)->backing_inode, mask);
}

int __init fuse_bpf_init(void)
{
	fuse_bpf_aio_request_cachep = kmem_cache_create("fuse_bpf_aio_req",
						   sizeof(struct fuse_bpf_aio_req),
						   0, SLAB_HWCACHE_ALIGN, NULL);
	if (!fuse_bpf_aio_request_cachep)
		return -ENOMEM;

	return 0;
}

void __exit fuse_bpf_cleanup(void)
{
	kmem_cache_destroy(fuse_bpf_aio_request_cachep);
}

ssize_t fuse_bpf_simple_request(struct fuse_mount *fm, struct fuse_bpf_args *bpf_args)
{
	int i;
	ssize_t res;
	struct fuse_args args = {
		.nodeid = bpf_args->nodeid,
		.opcode = bpf_args->opcode,
		.error_in = bpf_args->error_in,
		.in_numargs = bpf_args->in_numargs,
		.out_numargs = bpf_args->out_numargs,
		.force = !!(bpf_args->flags & FUSE_BPF_FORCE),
		.out_argvar = !!(bpf_args->flags & FUSE_BPF_OUT_ARGVAR),
	};

	for (i = 0; i < args.in_numargs; ++i)
		args.in_args[i] = (struct fuse_in_arg) {
			.size = bpf_args->in_args[i].size,
			.value = bpf_args->in_args[i].value,
		};
	for (i = 0; i < args.out_numargs; ++i)
		args.out_args[i] = (struct fuse_arg) {
			.size = bpf_args->out_args[i].size,
			.value = bpf_args->out_args[i].value,
		};

	res = fuse_simple_request(fm, &args);

	*bpf_args = (struct fuse_bpf_args) {
		.nodeid = args.nodeid,
		.opcode = args.opcode,
		.error_in = args.error_in,
		.in_numargs = args.in_numargs,
		.out_numargs = args.out_numargs,
	};
	if (args.force)
		bpf_args->flags |= FUSE_BPF_FORCE;
	if (args.out_argvar)
		bpf_args->flags |= FUSE_BPF_OUT_ARGVAR;
	for (i = 0; i < args.in_numargs; ++i)
		bpf_args->in_args[i] = (struct fuse_bpf_in_arg) {
			.size = args.in_args[i].size,
			.value = args.in_args[i].value,
		};
	for (i = 0; i < args.out_numargs; ++i)
		bpf_args->out_args[i] = (struct fuse_bpf_arg) {
			.size = args.out_args[i].size,
			.value = args.out_args[i].value,
		};
	return res;
}
