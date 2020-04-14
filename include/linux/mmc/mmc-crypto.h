/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019 MediaTek Inc.
 */

#ifndef _MMC_CRYPTO_H
#define _MMC_CRYPTO_H

struct mmc_host;
#ifdef CONFIG_MMC_CRYPTO
#include <linux/keyslot-manager.h>
#include <linux/mmc/host.h>
#include <linux/mmc/core.h>
#include <linux/blkdev.h>

struct keyslot_mgmt_ll_ops;

bool mmc_crypto_enable(struct mmc_host *host);

int mmc_init_crypto(struct mmc_host *host);

void mmc_crypto_setup_rq_keyslot_manager(struct mmc_host *host,
					 struct request_queue *q);

void mmc_crypto_destroy_rq_keyslot_manager(struct mmc_host *host);

void mmc_prepare_mqr_crypto_spec(struct mmc_host *host,
				 struct mmc_request *mrq,
				 struct request *req);

/* Crypto Variant Ops Support */
int mmc_prepare_mqr_crypto(struct mmc_host *host,
			   struct mmc_request *mrq,
			   struct request *req);
int mmc_complete_mqr_crypto(struct mmc_host *host);

#else /* CONFIG_MMC_CRYPTO */
static inline bool mmc_crypto_enable(struct mmc_host *host)
{
	return false;
}

static inline int mmc_init_crypto(struct mmc_host *host)
{
	return 0;
}

static inline void mmc_crypto_setup_rq_keyslot_manager(struct mmc_host *host,
						struct request_queue *q) { }

static inline void mmc_crypto_destroy_rq_keyslot_manager(struct mmc_host *host)
{ }

static inline int mmc_prepare_mqr_crypto(struct mmc_host *host,
					 struct mmc_request *mrq,
					 struct request *req)
{
	return 0;
}

static inline int mmc_complete_mqr_crypto(struct mmc_host *host)
{
	return 0;
}

#endif /* CONFIG_MMC_CRYPTO */

#endif /* _MMC_CRYPTO_H */
