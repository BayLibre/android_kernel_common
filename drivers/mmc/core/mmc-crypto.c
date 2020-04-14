// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 MediaTek Inc.
 */

#include <crypto/algapi.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc-crypto.h>
#include "queue.h"

/* Blk-crypto modes supported by UFS crypto */
static const struct mmc_crypto_alg_entry {
	enum mmc_crypto_alg mmc_alg;
	enum mmc_crypto_key_size mmc_key_size;
} mmc_crypto_algs[BLK_ENCRYPTION_MODE_MAX] = {
	[BLK_ENCRYPTION_MODE_AES_256_XTS] = {
		.mmc_alg = MMC_CRYPTO_ALG_AES_XTS,
		.mmc_key_size = MMC_CRYPTO_KEY_SIZE_256,
	},
};

static int mmc_crypto_keyslot_program(struct keyslot_manager *ksm,
				      const struct blk_crypto_key *key,
				      unsigned int slot)

{
	struct mmc_host *host = keyslot_manager_private(ksm);
	const union mmc_crypto_cap_entry *ccap_array = host->crypto_cap_array;
	const struct mmc_crypto_alg_entry *alg =
				&mmc_crypto_algs[key->crypto_mode];
	u8 data_unit_mask = key->data_unit_size / 512;
	int i;
	int cap_idx = -1;
	union mmc_crypto_cfg_entry cfg = { 0 };
	int err;

	BUILD_BUG_ON(MMC_CRYPTO_KEY_SIZE_INVALID != 0);
	for (i = 0; i < host->crypto_capabilities.num_crypto_cap; i++) {
		if (ccap_array[i].algorithm_id == alg->mmc_alg &&
		    ccap_array[i].key_size == alg->mmc_key_size &&
		    (ccap_array[i].sdus_mask & data_unit_mask)) {
			cap_idx = i;
			break;
		}
	}

	if (WARN_ON(cap_idx < 0))
		return -EOPNOTSUPP;

	cfg.data_unit_size = data_unit_mask;
	cfg.crypto_cap_idx = cap_idx;
	cfg.config_enable |= MMC_CRYPTO_CONFIGURATION_ENABLE;

	if (ccap_array[cap_idx].algorithm_id == MMC_CRYPTO_ALG_AES_XTS) {
		/* In XTS mode, the blk_crypto_key's size is already doubled */
		memcpy(cfg.crypto_key, key->raw, key->size/2);
		memcpy(cfg.crypto_key + MMC_CRYPTO_KEY_MAX_SIZE/2,
		       key->raw + key->size/2, key->size/2);
	} else {
		memcpy(cfg.crypto_key, key->raw, key->size);
	}

	err = host->crypto_vops->host_program_key(host, &cfg, slot);

	memzero_explicit(&cfg, sizeof(cfg));

	return err;
}

static void mmc_crypto_clear_keyslot(struct mmc_host *host, int slot)
{
	/*
	 * Clear the crypto cfg on the device. Clearing CFGE
	 * might not be sufficient, so just clear the entire cfg.
	 */
	union mmc_crypto_cfg_entry cfg = { 0 };

	host->crypto_vops->host_program_key(host, &cfg, slot);
}

static int mmc_crypto_keyslot_evict(struct keyslot_manager *ksm,
			const struct blk_crypto_key *key,
			unsigned int slot)
{
	struct mmc_host *host = keyslot_manager_private(ksm);

	mmc_crypto_clear_keyslot(host, slot);

	return 0;
}

static const struct keyslot_mgmt_ll_ops mmc_ksm_ops = {
	.keyslot_program	= mmc_crypto_keyslot_program,
	.keyslot_evict		= mmc_crypto_keyslot_evict,
};

bool mmc_crypto_enable(struct mmc_host *host)
{
	if (!(host->caps2 & MMC_CAP2_CRYPTO))
		return false;

	/* Reset might clear all keys, so reprogram all the keys. */
	keyslot_manager_reprogram_all_keys(host->ksm);
	return true;
}
EXPORT_SYMBOL_GPL(mmc_crypto_enable);

static enum blk_crypto_mode_num
mmc_find_blk_crypto_mode(union mmc_crypto_cap_entry cap)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mmc_crypto_algs); i++) {
		BUILD_BUG_ON(MMC_CRYPTO_KEY_SIZE_INVALID != 0);
		if (mmc_crypto_algs[i].mmc_alg == cap.algorithm_id &&
		    mmc_crypto_algs[i].mmc_key_size == cap.key_size) {
			return i;
		}
	}
	return BLK_ENCRYPTION_MODE_INVALID;
}

/**
 * mmc_init_crypto - Read crypto capabilities, init crypto fields in host
 * @host: Per adapter instance
 *
 * Return: 0 if crypto was initialized, or is not supported, else a -errno value
 */
int mmc_init_crypto(struct mmc_host *host)
{
	int cap_idx = 0;
	int err = 0;
	enum blk_crypto_mode_num blk_mode_num;
	int slot = 0;
	int num_keyslots;
	unsigned int crypto_modes_supported[BLK_ENCRYPTION_MODE_MAX] = {0};

	/*
	 * If host_init_crypto isn't specified, we assume there's no crypto
	 * support, since there isn't any specification on how to get the
	 * crypto_capabilities.
	 */
	if (!host->crypto_vops ||
	    !host->crypto_vops->host_init_crypto ||
	    !host->crypto_vops->get_crypto_capabilities ||
	    !host->crypto_vops->host_program_key)
		goto out;

	host->crypto_vops->host_init_crypto(host);

	/*
	 * Don't use crypto if the vendor specific driver hasn't advertised
	 * that crypto is supported.
	 */
	if (!(host->caps2 & MMC_CAP2_CRYPTO))
		goto out;

	host->crypto_cfg_register =
		(u32)host->crypto_capabilities.config_array_ptr * 0x100;

	host->crypto_cap_array =
		devm_kcalloc(&host->class_dev,
			     host->crypto_capabilities.num_crypto_cap,
			     sizeof(host->crypto_cap_array[0]), GFP_KERNEL);
	if (!host->crypto_cap_array) {
		err = -ENOMEM;
		goto out;
	}

	/*
	 * Store all the capabilities now so that we don't need to repeatedly
	 * access the device each time we want to know its capabilities
	 */
	err = host->crypto_vops->get_crypto_capabilities(host);
	if (err)
		goto out_free_caps;

	for (cap_idx = 0; cap_idx < host->crypto_capabilities.num_crypto_cap;
	     cap_idx++) {
		blk_mode_num = mmc_find_blk_crypto_mode(
					host->crypto_cap_array[cap_idx]);
		if (blk_mode_num == BLK_ENCRYPTION_MODE_INVALID)
			continue;
		crypto_modes_supported[blk_mode_num] |=
				host->crypto_cap_array[cap_idx].sdus_mask * 512;
	}

	num_keyslots = host->crypto_capabilities.config_count + 1;
	host->ksm = keyslot_manager_create(&host->class_dev, num_keyslots,
					   &mmc_ksm_ops,
					   BLK_CRYPTO_FEATURE_STANDARD_KEYS,
					   crypto_modes_supported, host);

	if (!host->ksm) {
		err = -ENOMEM;
		goto out_free_caps;
	}

	for (slot = 0; slot < num_keyslots; slot++)
		mmc_crypto_clear_keyslot(host, slot);

	return 0;
out_free_caps:
	devm_kfree(&host->class_dev, host->crypto_cap_array);
out:
	/* Indicate that init failed by clearing MMC_CAP2_CRYPTO */
	host->caps2 &= ~MMC_CAP2_CRYPTO;
	return err;
}
EXPORT_SYMBOL_GPL(mmc_init_crypto);

void mmc_crypto_setup_rq_keyslot_manager(struct mmc_host *host,
					 struct request_queue *q)
{
	if(!(host->caps2 & MMC_CAP2_CRYPTO) || !q)
		return;

	q->ksm = host->ksm;
}

void mmc_crypto_destroy_rq_keyslot_manager(struct mmc_host *host)
{
	keyslot_manager_destroy(host->ksm);
}

void mmc_prepare_mqr_crypto_spec(struct mmc_host *host,
				 struct mmc_request *mrq,
				 struct request *req)
{
	struct bio_crypt_ctx *bc;

	if (!req->bio ||
	    !bio_crypt_should_process(req)) {
		mrq->crypto_key_slot = -1;
		return;
	}

	bc = req->bio->bi_crypt_context;

	mrq->crypto_key_slot = bc->bc_keyslot;
	mrq->data_unit_num = bc->bc_dun[0];

	return;
}
EXPORT_SYMBOL_GPL(mmc_prepare_mqr_crypto_spec);

/* Crypto Variant Ops Support */

int mmc_complete_mqr_crypto(struct mmc_host *host)
{
	if (host->crypto_vops && host->crypto_vops->complete_mqr_crypto)
		return host->crypto_vops->complete_mqr_crypto(host);

	return 0;
}

int mmc_prepare_mqr_crypto(struct mmc_host *host,
			   struct mmc_request *mrq,
			   struct request *req)
{
	if (host->crypto_vops->prepare_mqr_crypto)
		return host->crypto_vops->prepare_mqr_crypto(host, mrq, req);
	mmc_prepare_mqr_crypto_spec(host, mrq, req);

	return 0;
}
