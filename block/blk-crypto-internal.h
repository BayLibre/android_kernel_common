/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2019 Google LLC
 */

#ifndef __LINUX_BLK_CRYPTO_INTERNAL_H
#define __LINUX_BLK_CRYPTO_INTERNAL_H

#include <linux/bio.h>
#include <linux/blkdev.h>

/* Represents a crypto mode supported by blk-crypto  */
struct blk_crypto_mode {
	const char *cipher_str; /* crypto API name (for fallback case) */
	unsigned int keysize; /* key size in bytes */
	unsigned int ivsize; /* iv size in bytes */
};

extern const struct blk_crypto_mode blk_crypto_modes[];

#ifdef CONFIG_BLK_INLINE_ENCRYPTION

void bio_crypt_ctx_init(void);

void bio_crypt_free_ctx(struct bio *bio);

void bio_crypt_dun_increment(u64 dun[BLK_CRYPTO_DUN_ARRAY_SIZE],
			     unsigned int inc);

void bio_crypt_advance(struct bio *bio, unsigned int bytes);

bool bio_crypt_rq_ctx_compatible(struct request *rq, struct bio *bio);

bool bio_crypt_ctx_mergeable(struct bio_crypt_ctx *bc1, unsigned int bc1_bytes,
			     struct bio_crypt_ctx *bc2);

static inline bool bio_crypt_ctx_back_mergeable(struct request *req,
						struct bio *bio)
{
	return bio_crypt_ctx_mergeable(req->crypt_ctx, blk_rq_bytes(req),
				       bio->bi_crypt_context);
}

static inline bool bio_crypt_ctx_front_mergeable(struct request *req,
						 struct bio *bio)
{
	return bio_crypt_ctx_mergeable(bio->bi_crypt_context,
				       bio->bi_iter.bi_size, req->crypt_ctx);
}

static inline bool bio_crypt_ctx_merge_rq(struct request *req,
					  struct request *next)
{
	return bio_crypt_ctx_mergeable(req->crypt_ctx, blk_rq_bytes(req),
				       next->crypt_ctx);
}

void blk_crypto_free_request(struct request *rq);

static inline void blk_crypto_rq_set_defaults(struct request *rq)
{
	rq->crypt_ctx = NULL;
	rq->crypt_keyslot = NULL;
}

static inline bool blk_crypto_rq_is_encrypted(struct request *rq)
{
	return rq->crypt_ctx;
}

blk_status_t blk_crypto_init_request(struct request *rq, struct bio *bio);

int blk_crypto_bio_prep(struct bio **bio_ptr);

void blk_crypto_rq_bio_prep(struct request *rq, struct bio *bio);

void blk_crypto_rq_prep_clone(struct request *dst, struct request *src);

blk_status_t blk_crypto_insert_cloned_request(struct request *rq);

#else /* CONFIG_BLK_INLINE_ENCRYPTION */

static inline void bio_crypt_ctx_init(void) { }

static inline void bio_crypt_free_ctx(struct bio *bio) { }

static inline void bio_crypt_advance(struct bio *bio, unsigned int bytes) { }

static inline bool bio_crypt_rq_ctx_compatible(struct request *rq,
					       struct bio *bio)
{
	return true;
}

static inline bool bio_crypt_ctx_front_mergeable(struct request *req,
						 struct bio *bio)
{
	return true;
}

static inline bool bio_crypt_ctx_back_mergeable(struct request *req,
						struct bio *bio)
{
	return true;
}

static inline bool bio_crypt_ctx_merge_rq(struct request *req,
					  struct request *next)
{
	return true;
}

static inline void blk_crypto_free_request(struct request *rq) { }

static inline void blk_crypto_rq_set_defaults(struct request *rq) { }

static inline bool blk_crypto_rq_is_encrypted(struct request *rq)
{
	return false;
}

static inline blk_status_t blk_crypto_init_request(struct request *rq,
						   struct bio *bio)
{
	return 0;
}

static inline int blk_crypto_bio_prep(struct bio **bio_ptr)
{
	return 0;
}

static inline void blk_crypto_rq_bio_prep(struct request *rq, struct bio *bio)
{ }

static inline void blk_crypto_rq_prep_clone(struct request *dst,
					    struct request *src) { }

static inline blk_status_t blk_crypto_insert_cloned_request(struct request *rq)
{
	return 0;
}

#endif /* CONFIG_BLK_INLINE_ENCRYPTION */

#ifdef CONFIG_BLK_INLINE_ENCRYPTION_FALLBACK

void blk_crypto_fallback_bio_prep(struct bio **bio_ptr);

int blk_crypto_fallback_evict_key(const struct blk_crypto_key *key);

#else /* CONFIG_BLK_INLINE_ENCRYPTION_FALLBACK */

static inline void blk_crypto_fallback_bio_prep(struct bio **bio_ptr)
{
	pr_warn_once("crypto API fallback disabled; failing request.\n");
	(*bio_ptr)->bi_status = BLK_STS_NOTSUPP;
}

static inline int
blk_crypto_fallback_evict_key(const struct blk_crypto_key *key)
{
	return 0;
}

#endif /* CONFIG_BLK_INLINE_ENCRYPTION_FALLBACK */

#endif /* __LINUX_BLK_CRYPTO_INTERNAL_H */
