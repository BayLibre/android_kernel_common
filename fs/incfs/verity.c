// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 Google LLC
 */

#include <linux/fsverity.h>

#include "data_mgmt.h"
#include "integrity.h"
#include "vfs.h"

static int incfs_begin_enable_verity(struct file *filp)
{
	pr_debug("Enter\n");
	return 0;
}

static int incfs_end_enable_verity(struct file *filp, const void *desc,
				   size_t desc_size, u64 merkle_tree_size)
{
	struct inode *inode = file_inode(filp);

	pr_debug("Enter\n");
	inode_set_flags(inode, S_VERITY, S_VERITY);
	return 0;
}

static int incfs_get_verity_descriptor(struct inode *inode, void *buf,
				      size_t buf_size)
{
	pr_debug("Enter\n");
	return 0;
}

static struct page *incfs_read_merkle_tree_page(struct inode *inode,
					       pgoff_t index,
					       unsigned long num_ra_pages)
{
	pr_debug("Enter\n");
	return NULL;
}

static int incfs_write_merkle_tree_block(struct inode *inode, const void *buf,
					u64 index, int log_blocksize)
{
	pr_debug("Enter\n");
	return 0;
}

static int incfs_get_root_hash(struct file *filp, u8 *root_hash)
{
	struct data_file *df = get_incfs_data_file(filp);

	pr_debug("Enter\n");
	if (!df)
		return -EINVAL;

	if (!df->df_hash_tree)
		return -EOPNOTSUPP;

	memcpy(root_hash, df->df_hash_tree->root_hash,
	       df->df_hash_tree->alg->digest_size);

	return 0;
}

int incfs_verity_get_flags(struct file *f, void __user *arg)
{
	u32 flags = (file_inode(f)->i_flags & S_VERITY) ? FS_VERITY_FL : 0;

	return put_user(flags, (int __user *) arg);
}

const struct fsverity_operations incfs_verityops = {
	.begin_enable_verity		= incfs_begin_enable_verity,
	.end_enable_verity		= incfs_end_enable_verity,
	.get_verity_descriptor		= incfs_get_verity_descriptor,
	.read_merkle_tree_page		= incfs_read_merkle_tree_page,
	.write_merkle_tree_block	= incfs_write_merkle_tree_block,
	.get_root_hash			= incfs_get_root_hash,
};
