/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2019 Google LLC
 */

#ifndef __LINUX_BLK_CRYPTO_H
#define __LINUX_BLK_CRYPTO_H

#include <linux/types.h>

#ifdef CONFIG_BLK_INLINE_ENCRYPTION

struct bio;

int blk_crypto_init(void);

int blk_crypto_submit_bio(struct bio **bio_ptr);

bool blk_crypto_endio(struct bio *bio);

#else /* CONFIG_BLK_INLINE_ENCRYPTION */

static inline int blk_crypto_init(void)
{
	return 0;
}

static inline int blk_crypto_submit_bio(struct bio **bio)
{
	return 0;
}

static inline bool blk_crypto_endio(struct bio *bio)
{
	return true;
}

#endif /* CONFIG_BLK_INLINE_ENCRYPTION */

#endif /* __LINUX_BLK_CRYPTO_H */
