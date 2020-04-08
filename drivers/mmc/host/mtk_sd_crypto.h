/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019 MediaTek Inc.
 */

#ifndef _MTK_SD_CRYPTO_H
#define _MTK_SD_CRYPTO_H

#ifdef CONFIG_MMC_CRYPTO
int cqe_crypto_start(struct mmc_host *host, struct mmc_request *mrq);
#else
static inline int cqe_crypto_start(struct mmc_host *host,
			struct mmc_request *mrq)
{
	return 0;
}
#endif

#endif
