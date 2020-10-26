// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 Google LLC
 */

#include <crypto/hash.h>
#include <crypto/sha2.h>
#include <linux/fsverity.h>
#include <linux/mount.h>

#include "verity.h"

#include "data_mgmt.h"
#include "integrity.h"
#include "vfs.h"

/*
 * The fs_verity implementation in incfs stores the file digest in the
 * data_file's df_verity_file_digest field.
 *
 * This can be set in one of two ways.
 *
 * When an incfs inode is opened, the file's metadata sections are loaded. If
 * one of them is a verity signature, the inode has the S_VERITY flag set and
 * the incfs data_file struct has the ->df_verity_signature field populated.
 *
 * Then when the inode is opened as a file, the digest of the
 * fsverity_descriptor is calculated, validated against the signature and
 * stored in df_verity_file_digest field of the data_file. For subsequent opens,
 * this field will be present and the open can simply proceed. The data is
 * stored for subsequent calls to FS_IOC_MEASURE_VERITY.
 *
 * Alternatively if there is no verity signature, the file is not a verity file.
 * When the ioctl FS_IOC_ENABLE_VERITY is called on the file, the same process
 * occurs - the digest of the fsverity_descriptor is calculated, validated
 * against the signature and stored in df_verity_file_digest field of the
 * data_file.
 *
 * Either way, ->df_verity_file_digest now remains until the inode is evicted.
 */


/* Largest digest size among all hash algorithms supported by fs-verity. */
#define FS_VERITY_MAX_DIGEST_SIZE	SHA512_DIGEST_SIZE

/* Arbitrary limit to bound the kmalloc() size.  Can be changed. */
#define FS_VERITY_MAX_DESCRIPTOR_SIZE	16384
#define FS_VERITY_MAX_SIGNATURE_SIZE	(FS_VERITY_MAX_DESCRIPTOR_SIZE - \
					 sizeof(struct fsverity_descriptor))

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

static int incfs_end_enable_verity(struct file *filp)
{
	struct inode *inode = file_inode(filp);

	inode_set_flags(inode, S_VERITY, S_VERITY);
	return 0;
}

static int compute_file_measurement(struct incfs_hash_alg *alg,
				    struct fsverity_descriptor *desc,
				    u8 *measurement)
{
	SHASH_DESC_ON_STACK(d, alg->shash);

	d->tfm = alg->shash;
	return crypto_shash_digest(d, (u8 *)desc, sizeof(*desc), measurement);
}

/*
 * Validate the given fsverity_descriptor and calculate its digest. The
 * signature (if present) is also checked.
 */
static struct mem_range fsverity_calc_digest(const struct inode *inode,
					     struct fsverity_descriptor *desc,
					     u8 *signature, size_t sig_size)
{
	struct mem_range verity_file_digest;
	int err;
	struct incfs_hash_alg *hash_alg;

	hash_alg = incfs_get_hash_alg(desc->hash_algorithm);
	if (IS_ERR(hash_alg))
		return range((u8 *)hash_alg, 0);

	verity_file_digest = range(kzalloc(FS_VERITY_MAX_DIGEST_SIZE,
					   GFP_KERNEL), hash_alg->digest_size);
	if (!verity_file_digest.data)
		return range(ERR_PTR(-ENOMEM), 0);

	err = compute_file_measurement(hash_alg, desc, verity_file_digest.data);
	if (err) {
		pr_err("Error %d computing file measurement", err);
		goto out;
	}
	pr_debug("Computed file measurement: %s:%*phN\n",
		 hash_alg->name, (int) verity_file_digest.len,
		 verity_file_digest.data);

	err = __fsverity_verify_signature(inode, desc->signature,
					  le32_to_cpu(desc->sig_size),
					  verity_file_digest.data,
					  FS_VERITY_HASH_ALG_SHA256);
out:
	if (err) {
		kfree(verity_file_digest.data);
		verity_file_digest = range(ERR_PTR(err), 0);
	}
	return verity_file_digest;
}

static inline struct mem_range fsverity_get_file_digest(struct inode *inode)
{
	struct inode_info *node = get_incfs_node(inode);
	struct data_file *df;

	if (!node) {
		pr_warn("Invalid inode\n");
		return range(NULL, 0);
	}

	df = node->n_file;
	return df->df_verity_file_digest;
}

static void fsverity_set_file_digest(struct inode *inode,
				     struct mem_range verity_file_digest)
{
	struct inode_info *node = get_incfs_node(inode);
	struct data_file *df;

	if (!node) {
		pr_warn("Invalid inode\n");
		return;
	}

	df = node->n_file;

	df->df_verity_file_digest = verity_file_digest;
}

static int enable_verity(struct file *filp,
			 const struct fsverity_enable_arg *arg)
{
	struct inode *inode = file_inode(filp);
	struct fsverity_descriptor *desc;
	u8 *signature = NULL;
	struct mem_range verity_file_digest;
	int err;

	/* Start initializing the fsverity_descriptor */
	desc = kzalloc(sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	desc->version = 1;
	desc->hash_algorithm = arg->hash_algorithm;
	desc->log_blocksize = ilog2(arg->block_size);
	desc->data_size = cpu_to_le64(inode->i_size);

	/* Get the signature if the user provided one */
	if (arg->sig_size) {
		signature = kzalloc(arg->sig_size, GFP_KERNEL);
		if (!signature) {
			err = -ENOMEM;
			goto out;
		}

		if (copy_from_user(signature, u64_to_user_ptr(arg->sig_ptr),
				   arg->sig_size)) {
			err = -EFAULT;
			goto out;
		}
	}

	err = incfs_get_root_hash(filp, arg, desc->root_hash);
	if (err)
		goto out;

	verity_file_digest = fsverity_calc_digest(inode, desc,
						  signature, arg->sig_size);
	if (IS_ERR(verity_file_digest.data)) {
		err = PTR_ERR(verity_file_digest.data);
		verity_file_digest.data = NULL;
		goto out;
	}

	err = incfs_end_enable_verity(filp);
	if (err)
		goto out;

	if (WARN_ON(!IS_VERITY(inode))) {
		err = -EINVAL;
		goto out;
	}

	/* Successfully enabled verity */
	fsverity_set_file_digest(inode, verity_file_digest);
	verity_file_digest.data = NULL;
out:
	kfree(signature);
	kfree(desc);
	kfree(verity_file_digest.data);
	if (err)
		pr_err("failed with err %d", err);
	return err;
}

int incfs_ioctl_enable_verity(struct file *filp, const void __user *uarg)
{
	struct inode *inode = file_inode(filp);
	struct fsverity_enable_arg arg;

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

	if (S_ISDIR(inode->i_mode))
		return -EISDIR;

	if (!S_ISREG(inode->i_mode))
		return -EINVAL;

	return enable_verity(filp, &arg);
}
