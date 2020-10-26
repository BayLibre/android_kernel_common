// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 Google LLC
 */

#include <crypto/hash.h>
#include <crypto/sha.h>
#include <linux/fsverity.h>
#include <linux/mount.h>

#include "verity.h"

#include "data_mgmt.h"
#include "integrity.h"
#include "vfs.h"

/*
 * Largest digest size among all hash algorithms supported by fs-verity.
 */
#define FS_VERITY_MAX_DIGEST_SIZE	SHA512_DIGEST_SIZE

/*
 * fsverity_info - cached verity metadata for an inode
 *
 * When a verity file is first opened, an instance of this struct is allocated
 * and stored in ->i_verity_info; it remains until the inode is evicted. It
 * caches the file measurement.
 */
struct fsverity_info {
	u8 measurement[FS_VERITY_MAX_DIGEST_SIZE];
};

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

/*
 * Merkle tree properties.  The file measurement is the hash of this structure
 * excluding the signature and with the sig_size field set to 0.
 */
struct fsverity_descriptor {
	__u8 version;		/* must be 1 */
	__u8 hash_algorithm;	/* Merkle tree hash algorithm */
	__u8 log_blocksize;	/* log2 of size of data and tree blocks */
	__u8 salt_size;		/* size of salt in bytes; 0 if none */
	__le32 sig_size;	/* size of signature in bytes; 0 if none */
	__le64 data_size;	/* size of file the Merkle tree is built over */
	__u8 root_hash[64];	/* Merkle tree root hash */
	__u8 salt[32];		/* salt prepended to each hashed block */
	__u8 __reserved[144];	/* must be 0's */
	__u8 signature[];	/* optional PKCS#7 signature */
} __packed;

/* Arbitrary limit to bound the kmalloc() size.  Can be changed. */
#define FS_VERITY_MAX_DESCRIPTOR_SIZE	16384

#define FS_VERITY_MAX_SIGNATURE_SIZE	(FS_VERITY_MAX_DESCRIPTOR_SIZE - \
					 sizeof(struct fsverity_descriptor))

static int compute_file_measurement(struct incfs_hash_alg *alg,
				    struct fsverity_descriptor *desc,
				    u8 *measurement)
{
	__le32 sig_size = desc->sig_size;
	int err;
	SHASH_DESC_ON_STACK(d, alg->shash);

	d->tfm = alg->shash;
	desc->sig_size = 0;
	err = crypto_shash_digest(d, (u8 *)desc, sizeof(*desc), measurement);
	desc->sig_size = sig_size;

	return err;
}

/*
 * Validate the given fsverity_descriptor and create a new fsverity_info from
 * it.  The signature (if present) is also checked.
 */
static struct fsverity_info *fsverity_create_info(const struct inode *inode,
					   void *_desc, size_t desc_size)
{
	struct fsverity_descriptor *desc = _desc;
	struct fsverity_info *vi;
	int err;
	struct incfs_hash_alg *hash_alg;

	if (desc_size < sizeof(*desc)) {
		pr_err("Unrecognized descriptor size: %zu bytes", desc_size);
		return ERR_PTR(-EINVAL);
	}

	if (desc->version != 1) {
		pr_err("Unrecognized descriptor version: %u", desc->version);
		return ERR_PTR(-EINVAL);
	}

	if (memchr_inv(desc->__reserved, 0, sizeof(desc->__reserved))) {
		pr_err("Reserved bits set in descriptor");
		return ERR_PTR(-EINVAL);
	}

	if (desc->salt_size > sizeof(desc->salt)) {
		pr_err("Invalid salt_size: %u", desc->salt_size);
		return ERR_PTR(-EINVAL);
	}

	if (le64_to_cpu(desc->data_size) != inode->i_size) {
		pr_err("Wrong data_size: %llu (desc) != %lld (inode)",
		       le64_to_cpu(desc->data_size), inode->i_size);
		return ERR_PTR(-EINVAL);
	}

	vi = kzalloc(sizeof(*vi), GFP_KERNEL);
	if (!vi)
		return ERR_PTR(-ENOMEM);

	hash_alg = incfs_get_hash_alg(desc->hash_algorithm);
	if (IS_ERR(hash_alg))
		return (struct fsverity_info *)hash_alg;

	err = compute_file_measurement(hash_alg, desc, vi->measurement);
	if (err) {
		pr_err("Error %d computing file measurement", err);
		goto out;
	}
	pr_debug("Computed file measurement: %s:%*phN\n",
		 hash_alg->name, hash_alg->digest_size, vi->measurement);

	/* TODO: better derivation of hash alg */
	err = __fsverity_verify_signature(inode, desc->signature,
					  le32_to_cpu(desc->sig_size),
					  vi->measurement,
					  FS_VERITY_HASH_ALG_SHA256);
out:
	if (err) {
		kfree(vi);
		vi = ERR_PTR(err);
	}
	return vi;
}

static void fsverity_set_info(struct inode *inode, struct fsverity_info *vi)
{
	/*
	 * Multiple tasks may race to set ->i_verity_info, so use
	 * cmpxchg_release().  This pairs with the smp_load_acquire() in
	 * fsverity_get_info().  I.e., here we publish ->i_verity_info with a
	 * RELEASE barrier so that other tasks can ACQUIRE it.
	 */
	if (cmpxchg_release(&inode->i_verity_info, NULL, vi) != NULL) {
		/* Lost the race, so free the fsverity_info we allocated. */
		kfree(vi);
		/*
		 * Afterwards, the caller may access ->i_verity_info directly,
		 * so make sure to ACQUIRE the winning fsverity_info.
		 */
		(void)fsverity_get_info(inode);
	}
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
	if (err)
		goto out;

	vi = fsverity_create_info(inode, desc, desc_size);
	if (IS_ERR(vi)) {
		err = PTR_ERR(vi);
		goto out;
	}

	/*
	 * Tell the filesystem to finish enabling verity on the file.
	 * Serialized with ->begin_enable_verity() by the inode lock.
	 */
	inode_lock(inode);
	err = incfs_end_enable_verity(filp, desc, desc_size);
	inode_unlock(inode);
	if (err) {
		pr_err("failed with err %d", err);
		kfree(vi);
	} else if (WARN_ON(!IS_VERITY(inode))) {
		err = -EINVAL;
		kfree(vi);
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

	if (arg.salt_size)
		return -EINVAL;

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
	 * allow_write_access() is needed to pair with deny_write_access().
	 * Regardless, the filesystem won't allow writing to verity files.
	 */
out_allow_write_access:
	allow_write_access(filp);
out_drop_write:
	mnt_drop_write_file(filp);
	return err;
}
