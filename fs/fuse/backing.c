/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2021 Paul Lawrence <paullawrence@google.com>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.
*/

#include "fuse_i.h"

#include <linux/filter.h>
#include <linux/namei.h>

#include "../internal.h"

int fuse_open_common_use_backing(struct file* file, bool isdir)
{
	/*
	 * For open, if the lookup was done passthrough there is no known use
	 * case for not passing through the open.
	 *
	 * Add bpf here if such a use case appears.
	 */

	struct bpf_fuse_data_kern ctx;
	struct fuse_inode *fuse_inode = get_fuse_inode(file->f_inode);
	struct dentry *entry = file->f_path.dentry;

	if (!fuse_inode || !fuse_inode->backing_inode)
		return false;

	ctx = (struct bpf_fuse_data_kern) {
		.fuse_opcode = isdir ? FUSE_OPENDIR : FUSE_OPEN,
	};
	strlcpy(ctx.name, entry->d_name.name, sizeof(ctx.name));
	return BPF_PROG_RUN(fuse_inode->bpf, &ctx);
}

int fuse_open_common_backing(struct inode *inode, struct file *file,
				 bool isdir)
{
	struct fuse_mount *fm = get_fuse_mount(inode);
	struct fuse_dentry *backing_fuse_dentry =
		get_fuse_dentry(file->f_path.dentry);
	struct fuse_file *fuse_file;
	struct file *backing_file;

	fuse_file = fuse_file_alloc(fm);
	if (!fuse_file)
		return -ENOMEM;
	file->private_data = fuse_file;

	backing_file = dentry_open(&backing_fuse_dentry->backing_path, O_RDWR,
				   current_cred());

	if (IS_ERR(backing_file))
		return PTR_ERR(backing_file);

	fuse_file->backing_file = backing_file;
	return 0;
}

bool fuse_release_use_backing(struct file* file)
{
	/*
	 * For release, if the lookup was done passthrough there is no known use
	 * case for not passing through the open.
	 *
	 * Add bpf here if such a use case appears.
	 */

	struct fuse_file *fuse_file = file->private_data;

	return fuse_file->backing_file;
}

int fuse_release_backing(struct inode *inode, struct file *file)
{
	struct fuse_file *fuse_file = file->private_data;

	fput(fuse_file->backing_file);
	return 0;
}

bool fuse_flush_use_backing(struct file* file)
{
	/*
	 * For flush, if the lookup was done passthrough there is no known use
	 * case for not passing through the open.
	 *
	 * Add bpf here if such a use case appears.
	 */

	struct fuse_file *fuse_file = file->private_data;

	return fuse_file->backing_file;
}

int fuse_flush_backing(struct file *file, fl_owner_t id)
{
	pr_debug("TODO: Paul\n");
	return 0;
}

bool fuse_readpage_use_backing(struct file *file, struct page *page)
{
	struct fuse_file *ff = file->private_data;
	struct fuse_inode *fuse_inode = get_fuse_inode(file->f_inode);
	struct bpf_fuse_data_kern ctx;

	if (!ff->backing_file || !fuse_inode || !fuse_inode->backing_inode)
		return false;

	ctx = (struct bpf_fuse_data_kern) {
		.fuse_opcode = FUSE_READ,
		.file_handle = ff->fh,
		.offset = page_offset(page),
	};
	return BPF_PROG_RUN(fuse_inode->bpf, &ctx) == 1;
}

int fuse_readpage_backing(struct file *file, struct page *page)
{
	struct fuse_file *ff = file->private_data;
	void *page_start = kmap(page);
	loff_t offset = page_offset(page);
	ssize_t res = kernel_read(ff->backing_file, page_start, PAGE_SIZE,
				  &offset);

	SetPageUptodate(page);
	flush_dcache_page(page);
	kunmap(page);
	unlock_page(page);
	return res;
}

bool fuse_readahead_use_backing(struct readahead_control *rac)
{
	struct fuse_file *ff = rac->file->private_data;

	if (!ff)
		return false;

	/* TODO call bpf to make this decision */
	return ff->backing_file;
}

void fuse_readahead_backing(struct readahead_control *rac)
{
	pr_debug("\n");
	return;
}

/*******************************************************************************
 * Directory operations after here                                             *
 ******************************************************************************/

bool fuse_lookup_use_backing(struct inode *dir, struct dentry *entry)
{
	struct bpf_fuse_data_kern ctx;
	struct fuse_inode *fuse_dir_inode = get_fuse_inode(dir);

	if (!fuse_dir_inode || !fuse_dir_inode->bpf)
		return false;

	ctx = (struct bpf_fuse_data_kern) {
		.fuse_opcode = FUSE_LOOKUP,
	};
	strlcpy(ctx.name, entry->d_name.name, sizeof(ctx.name));
	return BPF_PROG_RUN(fuse_dir_inode->bpf, &ctx) == 1;
}

struct dentry *fuse_lookup_backing(struct inode *dir, struct dentry *entry,
				   unsigned int flags)
{
	struct fuse_inode *dir_fuse_inode = get_fuse_inode(dir);
	struct fuse_dentry *dir_fuse_dentry = get_fuse_dentry(entry->d_parent);
	struct path *dir_backing_path = &dir_fuse_dentry->backing_path;
	struct dentry *newent = NULL;
	struct inode *inode = NULL;
	int err = 0;

	if (!dir_fuse_inode) {
		err = -EIO;
		goto out;
	}

	err = vfs_path_lookup(dir_backing_path->dentry, dir_backing_path->mnt,
		    entry->d_name.name, LOOKUP_FOLLOW,
		    &get_fuse_dentry(entry)->backing_path);

	/* TODO check negative dentries work correctly */
	if (err == -ENOENT) {
		d_add(entry, NULL);
		goto out;
	}

	if (err)
		goto out;

	inode = fuse_iget_backing(dir->i_sb,
			get_fuse_dentry(entry)->backing_path.dentry->d_inode);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out;
	}

	get_fuse_inode(inode)->bpf = dir_fuse_inode->bpf;
	newent = d_splice_alias(inode, entry);
	if (IS_ERR(newent)) {
		err = PTR_ERR(newent);
		goto out;
	}

out:
	if (err)
		return ERR_PTR(err);
	return newent;
}

bool fuse_getattr_use_backing(const struct path *path)
{
	struct bpf_fuse_data_kern ctx;
	struct dentry *entry = path->dentry;
	struct fuse_inode *fuse_inode = get_fuse_inode(entry->d_inode);

	if (!fuse_inode || !fuse_inode->bpf)
		return false;

	ctx = (struct bpf_fuse_data_kern) {
		.fuse_opcode = FUSE_GETATTR,
	};
	strlcpy(ctx.name, entry->d_name.name, sizeof(ctx.name));
	return BPF_PROG_RUN(fuse_inode->bpf, &ctx) == 1;
}

int fuse_getattr_backing(const struct path *path, struct kstat *stat,
			u32 request_mask, unsigned int flags)
{
	struct path *backing_path =
		&get_fuse_dentry(path->dentry)->backing_path;

	if (!stat)
		return 0;

	return vfs_getattr(backing_path, stat, request_mask, flags);
}

bool fuse_readdir_use_backing(struct file *file)
{
	struct bpf_fuse_data_kern ctx = {
		.fuse_opcode = FUSE_READDIR,
	};
	struct fuse_inode *fi = get_fuse_inode(file->f_inode);

	strlcpy(ctx.name, file->f_path.dentry->d_name.name, sizeof(ctx.name));
	pr_debug("Paul: %s\n", ctx.name);
	return BPF_PROG_RUN(fi->bpf, &ctx) == 1;
}

int fuse_readdir_backing(struct file *file, struct dir_context *ctx)
{
	struct fuse_file *ff = file->private_data;
	struct file *backing_dir = ff->backing_file;

	return iterate_dir(backing_dir, ctx);
}

bool fuse_access_use_backing(struct inode *inode)
{
	struct bpf_fuse_data_kern ctx = {
		.fuse_opcode = FUSE_ACCESS,
	};
	struct fuse_inode *fi = get_fuse_inode(inode);

	if (!fi || !fi->bpf)
		return false;
	return BPF_PROG_RUN(fi->bpf, &ctx) == 1;
}

int fuse_access_backing(struct inode *inode, int mask)
{
	struct fuse_inode *fi = get_fuse_inode(inode);

	return inode_permission(/* For mainline: init_user_ns,*/
				fi->backing_inode, mask);
}
