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

bool fuse_open_common_use_backing(struct file* file)
{
	/*
	 * For open, if the lookup was done passthrough there is no known use
	 * case for not passing through the open.
	 *
	 * Add bpf here if such a use case appears.
	 */

	return get_fuse_inode(file->f_inode)->backing_inode;
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

	pr_debug("Paul %s\n", file->f_path.dentry->d_name.name);
	backing_file = dentry_open(&backing_fuse_dentry->backing_path, O_RDWR,
				   current_cred());
	pr_debug("Paul %px\n", backing_file);

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

	return get_fuse_inode(file->f_inode)->backing_inode;
}

int fuse_release_backing(struct inode *inode, struct file *file)
{
	struct fuse_file *fuse_file = file->private_data;

	pr_debug("Paul\n");
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

	return get_fuse_inode(file->f_inode)->backing_inode;
}

int fuse_flush_backing(struct file *file, fl_owner_t id)
{
	pr_debug("Paul\n");
	return 0;
}

bool fuse_readpage_use_backing(struct file *file, struct page *page)
{
	struct fuse_file *ff = file->private_data;

	pr_debug("Paul\n");
	return ff->backing_file;
}

int fuse_readpage_backing(struct file *file, struct page *page)
{
	struct fuse_file *ff = file->private_data;
	void *page_start = kmap(page);
	loff_t offset = page_offset(page);
	ssize_t res = kernel_read(ff->backing_file, page_start, PAGE_SIZE,
				  &offset);

	pr_debug("Paul %lu %.*s\n", res, (int) res, (char *) page_start);
	dump_stack();
	pr_debug("Paul %llu\n", i_size_read(file->f_inode));
	SetPageUptodate(page);
	flush_dcache_page(page);
	kunmap(page);
	unlock_page(page);
	return res;
}

bool fuse_readahead_use_backing(struct readahead_control *rac)
{
	struct fuse_file *ff = rac->file->private_data;

	pr_debug("Paul\n");
	if (!ff)
		return false;

	/* TODO call bpf to make this decision */
	return ff->backing_file;
}

void fuse_readahead_backing(struct readahead_control *rac)
{
	pr_debug("Paul\n");
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



