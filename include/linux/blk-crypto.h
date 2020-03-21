/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2019 Google LLC
 */

#ifndef __LINUX_BLK_CRYPTO_H
#define __LINUX_BLK_CRYPTO_H

#include <linux/types.h>

enum blk_crypto_mode_num {
	BLK_ENCRYPTION_MODE_INVALID,
	BLK_ENCRYPTION_MODE_AES_256_XTS,
	BLK_ENCRYPTION_MODE_AES_128_CBC_ESSIV,
	BLK_ENCRYPTION_MODE_ADIANTUM,
	BLK_ENCRYPTION_MODE_MAX,
};

#define BLK_CRYPTO_MAX_KEY_SIZE		64
#define BLK_CRYPTO_MAX_WRAPPED_KEY_SIZE                128

/**
 * struct blk_crypto_key - an inline encryption key
 * @crypto_mode: encryption algorithm this key is for
 * @data_unit_size: the data unit size for all encryption/decryptions with this
 *	key.  This is the size in bytes of each individual plaintext and
 *	ciphertext.  This is always a power of 2.  It might be e.g. the
 *	filesystem block size or the disk sector size.
 * @data_unit_size_bits: log2 of data_unit_size
 * @dun_bytes: the maximum number of bytes of DUN used when using this key
 * @size: size of this key in bytes (determined by @crypto_mode)
 * @hash: hash of this key, for keyslot manager use only
 * @is_hw_wrapped: @raw points to a wrapped key to be used by an inline
 * 	encryption hardware that accepts wrapped keys.
 * @raw: the raw bytes of this key.  Only the first @size bytes are used.
 *
 * A blk_crypto_key is immutable once created, and many bios can reference it at
 * the same time.  It must not be freed until all bios using it have completed.
 */
struct blk_crypto_key {
	enum blk_crypto_mode_num crypto_mode;
	unsigned int data_unit_size;
	unsigned int data_unit_size_bits;
	unsigned int dun_bytes;
	unsigned int size;
	unsigned int hash;
	bool is_hw_wrapped;
	u8 raw[BLK_CRYPTO_MAX_WRAPPED_KEY_SIZE];
};

#define BLK_CRYPTO_MAX_IV_SIZE		32
#define BLK_CRYPTO_DUN_ARRAY_SIZE	(BLK_CRYPTO_MAX_IV_SIZE/sizeof(u64))

/**
 * struct bio_crypt_ctx - an inline encryption context
 * @bc_key: the key, algorithm, and data unit size to use
 * @bc_dun: the data unit number (starting IV) to use
 * @bc_keyslot: the keyslot that has been assigned for this key in @bc_ksm,
 *		or -1 if no keyslot has been assigned yet.
 * @bc_ksm: the keyslot manager into which the key has been programmed with
 *	    @bc_keyslot, or NULL if this key hasn't yet been programmed.
 *
 * A bio_crypt_ctx specifies that the contents of the bio will be encrypted (for
 * write requests) or decrypted (for read requests) inline by the storage device
 * or controller, or by the crypto API fallback.
 */
struct bio_crypt_ctx {
	const struct blk_crypto_key	*bc_key;
	u64				bc_dun[BLK_CRYPTO_DUN_ARRAY_SIZE];
};

#ifdef CONFIG_BLOCK

#include <linux/blk_types.h>
#include <linux/blkdev.h>

struct request;
struct request_queue;

#ifdef CONFIG_BLK_INLINE_ENCRYPTION

struct bio_crypt_ctx *bio_crypt_alloc_ctx(gfp_t gfp_mask);

void bio_crypt_set_ctx(struct bio *bio, const struct blk_crypto_key *key,
		       const u64 dun[BLK_CRYPTO_DUN_ARRAY_SIZE],
		       gfp_t gfp_mask);

static inline bool bio_has_crypt_ctx(struct bio *bio)
{
	return bio->bi_crypt_context;
}

void bio_crypt_clone(struct bio *dst, struct bio *src, gfp_t gfp_mask);

bool bio_crypt_dun_is_contiguous(const struct bio_crypt_ctx *bc,
				 unsigned int bytes,
				 u64 next_dun[BLK_CRYPTO_DUN_ARRAY_SIZE]);

int blk_crypto_init_key(struct blk_crypto_key *blk_key,
			const u8 *raw_key, unsigned int raw_key_size,
			bool is_hw_wrapped,
			enum blk_crypto_mode_num crypto_mode,
			unsigned int blk_crypto_dun_bytes,
			unsigned int data_unit_size);

int blk_crypto_evict_key(struct request_queue *q,
			 const struct blk_crypto_key *key);

#else /* CONFIG_BLK_INLINE_ENCRYPTION */

static inline bool bio_has_crypt_ctx(struct bio *bio)
{
	return false;
}

static inline void bio_crypt_clone(struct bio *dst, struct bio *src,
				   gfp_t gfp_mask) { }

#endif /* CONFIG_BLK_INLINE_ENCRYPTION */

#ifdef CONFIG_BLK_INLINE_ENCRYPTION_FALLBACK

int blk_crypto_start_using_key(struct blk_crypto_key *key,
			       struct request_queue *q);

#else /* CONFIG_BLK_INLINE_ENCRYPTION_FALLBACK */

static inline int blk_crypto_start_using_key(struct blk_crypto_key *key,
					     struct request_queue *q)
{
	return 0;
}

#endif /* CONFIG_BLK_INLINE_ENCRYPTION_FALLBACK */

#if IS_ENABLED(CONFIG_DM_DEFAULT_KEY)
static inline void bio_set_skip_dm_default_key(struct bio *bio)
{
	bio->bi_skip_dm_default_key = true;
}

static inline bool bio_should_skip_dm_default_key(const struct bio *bio)
{
	return bio->bi_skip_dm_default_key;
}

static inline void bio_clone_skip_dm_default_key(struct bio *dst,
						 const struct bio *src)
{
	dst->bi_skip_dm_default_key = src->bi_skip_dm_default_key;
}
#else /* CONFIG_DM_DEFAULT_KEY */
static inline void bio_set_skip_dm_default_key(struct bio *bio)
{
}

static inline bool bio_should_skip_dm_default_key(const struct bio *bio)
{
	return false;
}

static inline void bio_clone_skip_dm_default_key(struct bio *dst,
						 const struct bio *src)
{
}
#endif /* !CONFIG_DM_DEFAULT_KEY */

#endif /* CONFIG_BLOCK */

#endif /* __LINUX_BLK_CRYPTO_H */
