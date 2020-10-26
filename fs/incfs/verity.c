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
 * fs-verity integration into incfs
 *
 * Since incfs has its own merkle tree implementation, most of fs-verity code
 * is not needed. The key part that is needed is the signature check, since
 * that is based on the private /proc/sys/fs/verity/require_signatures value
 * and a private keyring. Thus the first change is to modify verity code to
 * export a version of fsverity_verify_signature.
 *
 * fs-verity integration then consists of the following modifications:
 *
 * 1. Add the (optional) verity signature to the incfs file format
 * 2. Add a pointer to the digest of the fs-verity descriptor struct to the
 *    data_file struct that incfs attaches to each file inode.
 * 3. Add the following ioclts:
 *  - FS_IOC_ENABLE_VERITY
 *  - FS_IOC_GETFLAGS
 *  - FS_IOC_MEASURE_VERITY
 * 4. When FS_IOC_ENABLE_VERITY is called on a non-verity file, the
 *    fs-verity descriptor struct is populated and digested. If it passes the
 *    signature check or the signature is NULL and
 *    fs.verity.require_signatures=0, then the S_VERITY flag is set, the digest
 *    is added to the data_file struct and the signature is added to the backing
 *    file. Note that an empty signature can still be added simply by adding
 *    an INCFS_MD_VERITY_SIGNATURE section but with vs_size and vs_offset set to
 *    zero
 * 5. When a file with an INCFS_MD_VERITY_SIGNATURE section is opened, the
 *    inode’s S_VERITY flag is set. If the file doesn’t have a digest, the
 *    file’s digest is calculated as above, checked, and set.
 * 6. FS_IOC_GETFLAGS simply returns the value of the S_VERITY flag
 * 7. FS_IOC_MEASURE_VERITY simply returns the stored digest
 * 8. The final complication is that if FS_IOC_ENABLE_VERITY is called on a file
 *    which doesn’t have a merkle tree, the merkle tree is calculated before the
 *    rest of the process is completed.
 */

/* Largest digest size among all hash algorithms supported by fs-verity. */
#define FS_VERITY_MAX_DIGEST_SIZE	SHA512_DIGEST_SIZE

/* Arbitrary limit to bound the kmalloc() size.  Can be changed. */
#define FS_VERITY_MAX_DESCRIPTOR_SIZE	16128
#define FS_VERITY_MAX_SIGNATURE_SIZE	(FS_VERITY_MAX_DESCRIPTOR_SIZE - \
					 sizeof(struct fsverity_descriptor))

static int incfs_get_root_hash(struct file *filp, u8 *root_hash)
{
	struct data_file *df = get_incfs_data_file(filp);

	if (!df)
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

static int incfs_compute_file_digest(struct incfs_hash_alg *alg,
				struct fsverity_descriptor *desc,
				u8 *digest)
{
	SHASH_DESC_ON_STACK(d, alg->shash);

	d->tfm = alg->shash;
	return crypto_shash_digest(d, (u8 *)desc, sizeof(*desc), digest);
}

static enum incfs_hash_tree_algorithm incfs_convert_fsverity_hash_alg(
								int hash_alg)
{
	switch (hash_alg) {
	case FS_VERITY_HASH_ALG_SHA256:
		return INCFS_HASH_TREE_SHA256;
	default:
		return -EINVAL;
	}
}

/*
 * Validate the given fsverity_descriptor and calculate its digest. The
 * signature (if present) is also checked.
 */
static struct mem_range incfs_calc_verity_digest(const struct inode *inode,
					     struct fsverity_descriptor *desc,
					     u8 *signature, size_t sig_size)
{
	enum incfs_hash_tree_algorithm incfs_hash_alg;
	struct mem_range verity_file_digest;
	int err;
	struct incfs_hash_alg *hash_alg;

	incfs_hash_alg = incfs_convert_fsverity_hash_alg(desc->hash_algorithm);
	if (incfs_hash_alg < 0)
		return range(ERR_PTR(incfs_hash_alg), 0);

	hash_alg = incfs_get_hash_alg(incfs_hash_alg);
	if (IS_ERR(hash_alg))
		return range((u8 *)hash_alg, 0);

	verity_file_digest = range(kzalloc(hash_alg->digest_size, GFP_KERNEL),
				   hash_alg->digest_size);
	if (!verity_file_digest.data)
		return range(ERR_PTR(-ENOMEM), 0);

	err = incfs_compute_file_digest(hash_alg, desc,
					verity_file_digest.data);
	if (err) {
		pr_err("Error %d computing file digest", err);
		goto out;
	}
	pr_debug("Computed file digest: %s:%*phN\n",
		 hash_alg->name, (int) verity_file_digest.len,
		 verity_file_digest.data);

	err = __fsverity_verify_signature(inode, desc->signature,
					  le32_to_cpu(desc->sig_size),
					  verity_file_digest.data,
					  desc->hash_algorithm);
out:
	if (err) {
		kfree(verity_file_digest.data);
		verity_file_digest = range(ERR_PTR(err), 0);
	}
	return verity_file_digest;
}

static struct mem_range incfs_get_verity_digest(struct inode *inode)
{
	struct inode_info *node = get_incfs_node(inode);
	struct data_file *df;
	struct mem_range verity_file_digest;

	if (!node) {
		pr_warn("Invalid inode\n");
		return range(NULL, 0);
	}

	df = node->n_file;

	/*
	 * Pairs with the cmpxchg_release() in fsverity_set_file_digest().
	 * I.e., another task may publish ->df_verity_file_digest concurrently,
	 * executing a RELEASE barrier.  We need to use smp_load_acquire() here
	 * to safely ACQUIRE the memory the other task published.
	 */
	verity_file_digest.data = smp_load_acquire(
					&df->df_verity_file_digest.data);
	verity_file_digest.len = df->df_verity_file_digest.len;
	return verity_file_digest;
}

static void incfs_set_verity_digest(struct inode *inode,
				     struct mem_range verity_file_digest)
{
	struct inode_info *node = get_incfs_node(inode);
	struct data_file *df;

	if (!node) {
		pr_warn("Invalid inode\n");
		return;
	}

	df = node->n_file;
	df->df_verity_file_digest.len = verity_file_digest.len;

	/*
	 * Multiple tasks may race to set ->df_verity_file_digest.data, so use
	 * cmpxchg_release().  This pairs with the smp_load_acquire() in
	 * fsverity_get_file_digest().  I.e., here we publish
	 * ->df_verity_file_digest.data, with a RELEASE barrier so that other
	 * tasks can ACQUIRE it.
	 */
	if (cmpxchg_release(&df->df_verity_file_digest.data, NULL,
			    verity_file_digest.data) != NULL)
		/* Lost the race, so free the file_digest we allocated. */
		kfree(verity_file_digest.data);
}

static int incfs_enable_verity(struct file *filp,
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
		signature = memdup_user(u64_to_user_ptr(arg->sig_ptr),
					arg->sig_size);
		if (IS_ERR(signature)) {
			err = PTR_ERR(signature);
			signature = NULL;
		}
	}

	err = incfs_get_root_hash(filp, desc->root_hash);
	if (err)
		goto out;

	verity_file_digest = incfs_calc_verity_digest(inode, desc, signature,
							arg->sig_size);
	if (IS_ERR(verity_file_digest.data)) {
		err = PTR_ERR(verity_file_digest.data);
		verity_file_digest.data = NULL;
		goto out;
	}

	err = incfs_end_enable_verity(filp);
	if (err)
		goto out;

	/* Successfully enabled verity */
	incfs_set_verity_digest(inode, verity_file_digest);
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

	if (arg.hash_algorithm != FS_VERITY_HASH_ALG_SHA256)
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

	return incfs_enable_verity(filp, &arg);
}
