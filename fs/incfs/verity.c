// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 Google LLC
 */

#include <crypto/sha.h>
#include <linux/fsverity.h>
#include <linux/mempool.h>
#include <linux/mount.h>
#include <linux/scatterlist.h>

#include "verity.h"

#include "data_mgmt.h"
#include "integrity.h"
#include "vfs.h"

/*
 * TODO - FIX ABUSIVE INCLUDE OF PRIVATE HEADER
 *
 * Needed for
 *	fsverity_create_info
 *	fsverity_set_info
 *	fsverity_free_info
 *
 * One solution - include these functions and all their called functions except
 * fsverity_verify_signature in incfs
 */
#include "../../fs/verity/fsverity_private.h"

static int incfs_get_root_hash(struct file *filp,
			       const struct fsverity_enable_arg *arg,
			       u8 *root_hash)
{
	struct data_file *df = get_incfs_data_file(filp);

	if (!df)
		return -EINVAL;

	if (arg->hash_algorithm != FS_VERITY_HASH_ALG_SHA256 ||
	    arg->block_size != INCFS_DATA_FILE_BLOCK_SIZE ||
	    arg->salt_size != 0)
		return -EINVAL;

	memcpy(root_hash, df->df_hash_tree->root_hash,
	       df->df_hash_tree->alg->digest_size);

	return 0;
}

static int incfs_end_enable_verity(struct file *filp, const void *desc,
				   size_t desc_size)
{
	struct inode *inode = file_inode(filp);

	inode_set_flags(inode, S_VERITY, S_VERITY);
	return 0;
}

static int enable_verity(struct file *filp,
			 const struct fsverity_enable_arg *arg)
{
	struct inode *inode = file_inode(filp);
	struct fsverity_descriptor *desc;
	size_t desc_size = sizeof(*desc) + arg->sig_size;
	struct fsverity_info *vi;
	int err;

	/* Start initializing the fsverity_descriptor */
	desc = kzalloc(desc_size, GFP_KERNEL);
	if (!desc)
		return -ENOMEM;
	desc->version = 1;
	desc->hash_algorithm = arg->hash_algorithm;
	desc->log_blocksize = ilog2(arg->block_size);

	/* Get the salt if the user provided one */
	if (arg->salt_size &&
	    copy_from_user(desc->salt, u64_to_user_ptr(arg->salt_ptr),
			   arg->salt_size)) {
		err = -EFAULT;
		goto out;
	}
	desc->salt_size = arg->salt_size;

	/* Get the signature if the user provided one */
	if (arg->sig_size &&
	    copy_from_user(desc->signature, u64_to_user_ptr(arg->sig_ptr),
			   arg->sig_size)) {
		err = -EFAULT;
		goto out;
	}
	desc->sig_size = cpu_to_le32(arg->sig_size);

	desc->data_size = cpu_to_le64(inode->i_size);

	/*
	 * Start enabling verity on this file, serialized by the inode lock.
	 * Fail if verity is already enabled or is already being enabled.
	 */
	inode_lock(inode);
	err = IS_VERITY(inode) ? -EEXIST : 0;
	inode_unlock(inode);
	if (err)
		goto out;

	err = incfs_get_root_hash(filp, arg, desc->root_hash);

	/*
	 * Create the fsverity_info.  Don't bother trying to save work by
	 * reusing the merkle_tree_params from above.  Instead, just create the
	 * fsverity_info from the fsverity_descriptor as if it were just loaded
	 * from disk.  This is simpler, and it serves as an extra check that the
	 * metadata we're writing is valid before actually enabling verity.
	 */
	vi = fsverity_create_info(inode, desc, desc_size);
	if (IS_ERR(vi)) {
		err = PTR_ERR(vi);
		goto out;
	}

	if (arg->sig_size)
		pr_debug("Storing a %u-byte PKCS#7 signature alongside the file\n",
			 arg->sig_size);

	/*
	 * Tell the filesystem to finish enabling verity on the file.
	 * Serialized with ->begin_enable_verity() by the inode lock.
	 */
	inode_lock(inode);
	err = incfs_end_enable_verity(filp, desc, desc_size);
	inode_unlock(inode);
	if (err) {
		pr_err("failed with err %d", err);
		fsverity_free_info(vi);
	} else if (WARN_ON(!IS_VERITY(inode))) {
		err = -EINVAL;
		fsverity_free_info(vi);
	} else {
		/* Successfully enabled verity */

		/*
		 * Readers can start using ->i_verity_info immediately, so it
		 * can't be rolled back once set.  So don't set it until just
		 * after the filesystem has successfully enabled verity.
		 */
		fsverity_set_info(inode, vi);
	}
out:
	kfree(desc);
	return err;
}

int ioctl_enable_verity(struct file *filp, const void __user *uarg)
{
	struct inode *inode = file_inode(filp);
	struct fsverity_enable_arg arg;
	int err;

	if (copy_from_user(&arg, uarg, sizeof(arg)))
		return -EFAULT;

	if (arg.version != 1)
		return -EINVAL;

	if (arg.__reserved1 ||
	    memchr_inv(arg.__reserved2, 0, sizeof(arg.__reserved2)))
		return -EINVAL;

	if (arg.block_size != PAGE_SIZE)
		return -EINVAL;

	if (arg.salt_size > sizeof_field(struct fsverity_descriptor, salt))
		return -EMSGSIZE;

	if (arg.sig_size > FS_VERITY_MAX_SIGNATURE_SIZE)
		return -EMSGSIZE;

	/*
	 * Require a regular file with write access.  But the actual fd must
	 * still be readonly so that we can lock out all writers.  This is
	 * needed to guarantee that no writable fds exist to the file once it
	 * has verity enabled, and to stabilize the data being hashed.
	 */

	err = inode_permission(inode, MAY_WRITE);
	if (err)
		return err;

	if (IS_APPEND(inode))
		return -EPERM;

	if (S_ISDIR(inode->i_mode))
		return -EISDIR;

	if (!S_ISREG(inode->i_mode))
		return -EINVAL;

	err = mnt_want_write_file(filp);
	if (err) /* -EROFS */
		return err;

	err = deny_write_access(filp);
	if (err) /* -ETXTBSY */
		goto out_drop_write;

	err = enable_verity(filp, &arg);
	if (err)
		goto out_allow_write_access;

	/*
	 * Some pages of the file may have been evicted from pagecache after
	 * being used in the Merkle tree construction, then read into pagecache
	 * again by another process reading from the file concurrently.  Since
	 * these pages didn't undergo verification against the file measurement
	 * which fs-verity now claims to be enforcing, we have to wipe the
	 * pagecache to ensure that all future reads are verified.
	 */
	filemap_write_and_wait(inode->i_mapping);
	invalidate_inode_pages2(inode->i_mapping);

	/*
	 * allow_write_access() is needed to pair with deny_write_access().
	 * Regardless, the filesystem won't allow writing to verity files.
	 */
out_allow_write_access:
	allow_write_access(filp);
out_drop_write:
	mnt_drop_write_file(filp);
	return err;
}

int incfs_verity_get_flags(struct file *f, void __user *arg)
{
	u32 flags = (file_inode(f)->i_flags & S_VERITY) ? FS_VERITY_FL : 0;

	return put_user(flags, (int __user *) arg);
}

