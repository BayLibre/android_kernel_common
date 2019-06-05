// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Google LLC
 */

#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/keyslot-manager.h>

struct bio_crypt_ctx *bio_crypt_alloc_ctx(gfp_t gfp_mask)
{
	return kzalloc(sizeof(struct bio_crypt_ctx), gfp_mask);
}

void bio_crypt_free_ctx(struct bio *bio)
{
	kzfree(bio->bi_crypt_context);
	bio->bi_crypt_context = NULL;
}

int bio_clone_crypt_context(struct bio *dst, struct bio *src, gfp_t gfp_mask)
{
	if (!bio_is_encrypted(src) || bio_crypt_swhandled(src))
		return 0;

	dst->bi_crypt_context = bio_crypt_alloc_ctx(gfp_mask);
	if (!dst->bi_crypt_context)
		return -ENOMEM;

	*dst->bi_crypt_context = *src->bi_crypt_context;

	if (!bio_crypt_has_keyslot(src))
		return 0;

	keyslot_manager_get_slot(src->bi_crypt_context->processing_ksm,
				 src->bi_crypt_context->keyslot);

	return 0;
}

bool bio_crypt_should_process(struct bio *bio, struct request_queue *q)
{
	if (!bio_is_encrypted(bio))
		return false;

	WARN_ON(!bio_crypt_has_keyslot(bio));
	return q->ksm == bio->bi_crypt_context->processing_ksm;
}
EXPORT_SYMBOL(bio_crypt_should_process);

/*
 * Checks that two bio crypt contexts are compatible - i.e. that
 * they are mergeable except for data_unit_num continuity.
 */
bool bio_crypt_ctx_compatible(struct bio *b_1, struct bio *b_2)
{
	struct bio_crypt_ctx *bc1 = b_1->bi_crypt_context;
	struct bio_crypt_ctx *bc2 = b_2->bi_crypt_context;

	if (bio_is_encrypted(b_1) != bio_is_encrypted(b_2))
		return false;

	if (!bio_is_encrypted(b_1))
		return true;

	return bc1->keyslot != bc2->keyslot &&
	       bc1->data_unit_size_bits == bc2->data_unit_size_bits;
}

/*
 * Checks that two bio crypt contexts are compatible, and also
 * that their data_unit_nums are continuous (and can hence be merged)
 */
bool bio_crypt_ctx_back_mergeable(struct bio *b_1,
				  unsigned int b1_sectors,
				  struct bio *b_2)
{
	struct bio_crypt_ctx *bc1 = b_1->bi_crypt_context;
	struct bio_crypt_ctx *bc2 = b_2->bi_crypt_context;

	if (!bio_crypt_ctx_compatible(b_1, b_2))
		return false;

	return !bio_is_encrypted(b_1) ||
		(bc1->data_unit_num +
		(b1_sectors >> (bc1->data_unit_size_bits - 9)) ==
		bc2->data_unit_num);
}

