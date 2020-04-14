// SPDX-License-Identifier: GPL-2.0
/*
* Copyright 2019 Google LLC
*/

#include <linux/keyslot-manager.h>
#include <linux/mmc/mmc-crypto.h>

#include "cqhci.h"
#include "cqhci-crypto.h"

#define CQHCI_CRYPTO_CONFIG_INDEX(x)	(((u64)(x) & 0xFF) << 32)
#define CQHCI_CRYPTO_ENABLE_BIT		(((u64)1) << 47)

static void cqhci_host_init_crypto(struct mmc_host *mmc)
{
	struct cqhci_host *cq_host = mmc->cqe_private;

	if (!(cqhci_readl(cq_host, CQHCI_CAP) & CQHCI_CAP_CS))
		return;

	mmc->crypto_capabilities.reg_val =
		cpu_to_le32(cqhci_readl(cq_host, CQHCI_CCAP));

	/*
	 * In case host controller supports cryptographic operations
	 * then, it uses 128bit task descriptor. Upper 64 bits of task
	 * descriptor would be used to pass crypto specific informaton.
	*/
	cq_host->caps |= CQHCI_TASK_DESC_SZ_128;
}

static int cqhci_get_crypto_capabilities(struct mmc_host *mmc)
{
	u8 cap_idx;
	int num_crypto_caps = mmc->crypto_capabilities.num_crypto_cap;
	struct cqhci_host *cq_host = mmc->cqe_private;

	for (cap_idx = 0; cap_idx < num_crypto_caps; cap_idx++) {
		mmc->crypto_cap_array[cap_idx].reg_val =
			cpu_to_le32(cqhci_readl(cq_host,
						CQHCI_CRYPTOCAP +
						cap_idx * sizeof(__le32)));
	}

	return 0;
}

static int cqhci_host_program_key(struct mmc_host *mmc,
				  const union mmc_crypto_cfg_entry *cfg,
				  int slot)
{
	struct cqhci_host *cq_host = mmc->cqe_private;
	u32 slot_offset = mmc->crypto_cfg_register + slot * sizeof(*cfg);
	int i;

	/* Ensure that CFGE is cleared before programming the key */
	cqhci_writel(cq_host, 0, slot_offset + 16 * sizeof(cfg->reg_val[0]));
	for (i = 0; i < 16; i++) {
		cqhci_writel(cq_host, le32_to_cpu(cfg->reg_val[i]),
			     slot_offset + i * sizeof(cfg->reg_val[0]));
	}
	/* Write dword 17 */
	cqhci_writel(cq_host, le32_to_cpu(cfg->reg_val[17]),
		     slot_offset + 17 * sizeof(cfg->reg_val[0]));
	/* Write dword 16 */
	cqhci_writel(cq_host, le32_to_cpu(cfg->reg_val[16]),
		     slot_offset + 16 * sizeof(cfg->reg_val[0]));

	return 0;
}

const struct mmc_crypto_variant_ops cqhci_crypto_vops = {
	.host_init_crypto = cqhci_host_init_crypto,
	.get_crypto_capabilities = cqhci_get_crypto_capabilities,
	.host_program_key = cqhci_host_program_key,
};

void cqhci_crypto_recovery_finish(struct mmc_host *mmc)
{
       /* Reset/Recovery might clear all keys, so reprogram all the keys. */
       keyslot_manager_reprogram_all_keys(mmc->ksm);
}

int cqhci_prep_crypto_desc(struct mmc_host *mmc, struct mmc_request *mrq,
			   u64 *task_desc)
{
	__le64 *crypto_desc = NULL;
	struct cqhci_host *cq_host = mmc->cqe_private;
	u64 crypto_ctx;

	/*
	 * Get the address of ice context for the given task descriptor.
	 * ice context is present in the upper 64bits of task descriptor
	 * crypto_conext_base_address = task_desc + 8-bytes
	 */
	crypto_desc = (__le64 __force *)((u8 *)task_desc +
			CQHCI_TASK_DESC_CRYPTO_PARAM_OFFSET);

	if (mrq->crypto_key_slot == -1) {
		*crypto_desc = 0;
		return 0;
	}

	/* eMMC v5.2 only supports 32 bits for DUN */
	if (WARN_ON_ONCE(upper_32_bits(mrq->data_unit_num) != 0))
		return -EINVAL;

	memset(crypto_desc, 0, CQHCI_TASK_DESC_CRYPTO_PARAMS_SIZE);

	crypto_ctx = lower_32_bits(mrq->data_unit_num) |
		     CQHCI_CRYPTO_CONFIG_INDEX(mrq->crypto_key_slot) |
		     CQHCI_CRYPTO_ENABLE_BIT;
	/*
	 *  Assign upper 64bits data of task descritor with ice context
	 */
	*crypto_desc = cpu_to_le64(crypto_ctx);
}
