//SPDX-License-Identifier: GPL-2.0
/*
* Copyright 2019 Google LLC
*/

#ifndef LINUX_MMC_CQHCI_CRYPTO_H
#define LINUX_MMC_CQHCI_CRYPTO_H

#include <linux/mmc/host.h>

#ifdef CONFIG_MMC_CRYPTO

extern const struct mmc_crypto_variant_ops cqhci_crypto_vops;

void cqhci_crypto_recovery_finish(struct mmc_host *mmc);

int cqhci_prep_crypto_desc(struct mmc_host *mmc, struct mmc_request *mrq,
			   u64 *task_desc);

#else /* CONFIG_MMC_CRYPTO */

void cqhci_crypto_recovery_finish(struct mmc_host *mmc) { }

static inline int cqhci_prep_crypto_desc(struct mmc_host *mmc,
					 struct mmc_request *mrq,
					 u64 *task_desc)
{
	return 0;
}

#endif /* CONFIG_MMC_CRYPTO */

#endif /* LINUX_MMC_CQHCI_CRYPTO_H */
