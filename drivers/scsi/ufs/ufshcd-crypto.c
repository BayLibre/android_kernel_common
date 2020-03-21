// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Google LLC
 */

#include <linux/keyslot-manager.h>
#include "ufshcd.h"
#include "ufshcd-crypto.h"

static inline int ufshcd_num_keyslots(struct ufs_hba *hba)
{
	return hba->crypto_capabilities.config_count + 1;
}

static inline bool ufshcd_keyslot_valid(struct ufs_hba *hba, unsigned int slot)
{
	/*
	 * The actual number of configurations supported is (CFGC+1), so slot
	 * numbers range from 0 to config_count inclusive.
	 */
	return slot < ufshcd_num_keyslots(hba);
}

static bool ufshcd_cap_idx_valid(struct ufs_hba *hba, unsigned int cap_idx)
{
	return cap_idx < hba->crypto_capabilities.num_crypto_cap;
}

static u8 ufshcd_get_data_unit_size_mask(unsigned int data_unit_size)
{
	if (data_unit_size < 512 || data_unit_size > 65536 ||
	    !is_power_of_2(data_unit_size))
		return 0;

	return data_unit_size / 512;
}

static size_t ufshcd_get_keysize_bytes(enum ufs_crypto_key_size size)
{
	switch (size) {
	case UFS_CRYPTO_KEY_SIZE_128:
		return 16;
	case UFS_CRYPTO_KEY_SIZE_192:
		return 24;
	case UFS_CRYPTO_KEY_SIZE_256:
		return 32;
	case UFS_CRYPTO_KEY_SIZE_512:
		return 64;
	default:
		return 0;
	}
}

/* Blk-crypto modes supported by UFS crypto */
static const struct {
	enum ufs_crypto_alg ufs_alg;
	enum ufs_crypto_key_size ufs_key_size;
	enum blk_crypto_mode_num blk_mode;
	bool supported;
} ufs_crypto_algs[BLK_ENCRYPTION_MODE_MAX] = {
	[BLK_ENCRYPTION_MODE_AES_256_XTS] = {
		.ufs_alg = UFS_CRYPTO_ALG_AES_XTS,
		.ufs_key_size = UFS_CRYPTO_KEY_SIZE_256,
		.blk_mode = BLK_ENCRYPTION_MODE_AES_256_XTS,
		.supported = true,
	},
};

int ufshcd_crypto_cap_find(struct ufs_hba *hba,
			   enum blk_crypto_mode_num crypto_mode,
			   unsigned int data_unit_size)
{
	enum ufs_crypto_alg ufs_alg;
	u8 data_unit_mask;
	int cap_idx;
	enum ufs_crypto_key_size ufs_key_size;
	union ufs_crypto_cap_entry *ccap_array = hba->crypto_cap_array;

	if (!ufshcd_hba_is_crypto_supported(hba))
		return -EINVAL;

	if (!ufs_crypto_algs[crypto_mode].supported)
		return -EINVAL;

	ufs_alg = ufs_crypto_algs[crypto_mode].ufs_alg;
	ufs_key_size = ufs_crypto_algs[crypto_mode].ufs_key_size;

	data_unit_mask = ufshcd_get_data_unit_size_mask(data_unit_size);

	for (cap_idx = 0; cap_idx < hba->crypto_capabilities.num_crypto_cap;
	     cap_idx++) {
		if (ccap_array[cap_idx].algorithm_id == ufs_alg &&
		    (ccap_array[cap_idx].sdus_mask & data_unit_mask) &&
		    ccap_array[cap_idx].key_size == ufs_key_size)
			return cap_idx;
	}

	return -EINVAL;
}
EXPORT_SYMBOL(ufshcd_crypto_cap_find);

/**
 * ufshcd_crypto_cfg_entry_write_key - Write a key into a crypto_cfg_entry
 *
 *	Writes the key with the appropriate format - for AES_XTS,
 *	the first half of the key is copied as is, the second half is
 *	copied with an offset halfway into the cfg->crypto_key array.
 *	For the other supported crypto algs, the key is just copied.
 *
 * @cfg: The crypto config to write to
 * @key: The key to write
 * @cap: The crypto capability (which specifies the crypto alg and key size)
 *
 * Returns BLK_STS_OK on success, or BLK_STS_IOERR on error
 */
static blk_status_t ufshcd_crypto_cfg_entry_write_key(
						union ufs_crypto_cfg_entry *cfg,
						const u8 *key,
						union ufs_crypto_cap_entry cap)
{
	size_t key_size_bytes = ufshcd_get_keysize_bytes(cap.key_size);

	if (key_size_bytes == 0)
		return BLK_STS_IOERR;

	switch (cap.algorithm_id) {
	case UFS_CRYPTO_ALG_AES_XTS:
		key_size_bytes *= 2;
		if (key_size_bytes > UFS_CRYPTO_KEY_MAX_SIZE)
			return BLK_STS_IOERR;

		memcpy(cfg->crypto_key, key, key_size_bytes/2);
		memcpy(cfg->crypto_key + UFS_CRYPTO_KEY_MAX_SIZE/2,
		       key + key_size_bytes/2, key_size_bytes/2);
		return BLK_STS_OK;
	case UFS_CRYPTO_ALG_BITLOCKER_AES_CBC:
		/* fall through */
	case UFS_CRYPTO_ALG_AES_ECB:
		/* fall through */
	case UFS_CRYPTO_ALG_ESSIV_AES_CBC:
		memcpy(cfg->crypto_key, key, key_size_bytes);
		return BLK_STS_OK;
	}

	return BLK_STS_IOERR;
}

static blk_status_t ufshcd_program_key(struct ufs_hba *hba,
				       const union ufs_crypto_cfg_entry *cfg,
				       int slot)
{
	int i;
	u32 slot_offset = hba->crypto_cfg_register + slot * sizeof(*cfg);
	blk_status_t err;

	ufshcd_hold(hba, false);

	if (hba->vops->program_key) {
		err = hba->vops->program_key(hba, cfg, slot);
		goto out;
	}

	/* Ensure that CFGE is cleared before programming the key */
	ufshcd_writel(hba, 0, slot_offset + 16 * sizeof(cfg->reg_val[0]));
	for (i = 0; i < 16; i++) {
		ufshcd_writel(hba, le32_to_cpu(cfg->reg_val[i]),
			      slot_offset + i * sizeof(cfg->reg_val[0]));
	}
	/* Write dword 17 */
	ufshcd_writel(hba, le32_to_cpu(cfg->reg_val[17]),
		      slot_offset + 17 * sizeof(cfg->reg_val[0]));
	/* Dword 16 must be written last */
	ufshcd_writel(hba, le32_to_cpu(cfg->reg_val[16]),
		      slot_offset + 16 * sizeof(cfg->reg_val[0]));
	err = BLK_STS_OK;
out:
	ufshcd_release(hba);
	return err;
}

static void ufshcd_clear_keyslot(struct ufs_hba *hba, int slot)
{
	union ufs_crypto_cfg_entry cfg = { 0 };
	int err;

	err = ufshcd_program_key(hba, &cfg, slot);
	WARN_ON_ONCE(err);
}

/* Clear all keyslots at driver init time */
static void ufshcd_clear_all_keyslots(struct ufs_hba *hba)
{
	int slot;

	for (slot = 0; slot < ufshcd_num_keyslots(hba); slot++)
		ufshcd_clear_keyslot(hba, slot);
}

static blk_status_t ufshcd_crypto_keyslot_program(struct keyslot_manager *ksm,
					const struct blk_crypto_key *key,
					unsigned int slot)
{
	struct ufs_hba *hba = container_of(ksm, struct ufs_hba, ksm);
	blk_status_t err = BLK_STS_OK;
	u8 data_unit_mask;
	union ufs_crypto_cfg_entry cfg;
	int cap_idx;

	cap_idx = ufshcd_crypto_cap_find(hba, key->crypto_mode,
					 key->data_unit_size);

	if (!(hba->caps & UFSHCD_CAP_CRYPTO) ||
	    !ufshcd_keyslot_valid(hba, slot) ||
	    !ufshcd_cap_idx_valid(hba, cap_idx))
		return BLK_STS_IOERR;

	data_unit_mask = ufshcd_get_data_unit_size_mask(key->data_unit_size);

	if (!(data_unit_mask & hba->crypto_cap_array[cap_idx].sdus_mask))
		return BLK_STS_IOERR;

	memset(&cfg, 0, sizeof(cfg));
	cfg.data_unit_size = data_unit_mask;
	cfg.crypto_cap_idx = cap_idx;
	cfg.config_enable |= UFS_CRYPTO_CONFIGURATION_ENABLE;

	err = ufshcd_crypto_cfg_entry_write_key(&cfg, key->raw,
						hba->crypto_cap_array[cap_idx]);
	if (err)
		return err;

	err = ufshcd_program_key(hba, &cfg, slot);

	memzero_explicit(&cfg, sizeof(cfg));

	return err;
}

static int ufshcd_crypto_keyslot_evict(struct keyslot_manager *ksm,
				       const struct blk_crypto_key *key,
				       unsigned int slot)
{
	struct ufs_hba *hba = container_of(ksm, struct ufs_hba, ksm);

	if (!(hba->caps & UFSHCD_CAP_CRYPTO) ||
	    !ufshcd_keyslot_valid(hba, slot))
		return -EINVAL;

	/*
	 * Clear the crypto cfg on the device. Clearing CFGE
	 * might not be sufficient, so just clear the entire cfg.
	 */
	ufshcd_clear_keyslot(hba, slot);

	return 0;
}

/* Functions implementing UFSHCI v2.1 specification behaviour */
void ufshcd_crypto_enable_spec(struct ufs_hba *hba)
{
	if (!ufshcd_hba_is_crypto_supported(hba))
		return;

	hba->caps |= UFSHCD_CAP_CRYPTO;

	/* Reset might clear all keys, so reprogram all the keys. */
	blk_ksm_reprogram_all_keys(&hba->ksm);
}
EXPORT_SYMBOL(ufshcd_crypto_enable_spec);

void ufshcd_crypto_disable_spec(struct ufs_hba *hba)
{
	hba->caps &= ~UFSHCD_CAP_CRYPTO;
}
EXPORT_SYMBOL(ufshcd_crypto_disable_spec);

static const struct keyslot_mgmt_ll_ops ufshcd_ksm_ops = {
	.keyslot_program	= ufshcd_crypto_keyslot_program,
	.keyslot_evict		= ufshcd_crypto_keyslot_evict,
};

static enum blk_crypto_mode_num ufshcd_blk_crypto_mode_num_for_alg_keysize(
					enum ufs_crypto_alg ufs_crypto_alg,
					enum ufs_crypto_key_size key_size)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ufs_crypto_algs); i++) {
		if (ufs_crypto_algs[i].supported &&
		    ufs_crypto_algs[i].ufs_alg == ufs_crypto_alg &&
		    ufs_crypto_algs[i].ufs_key_size == key_size) {
			return ufs_crypto_algs[i].blk_mode;
		}
	}
	return BLK_ENCRYPTION_MODE_INVALID;
}

/**
 * ufshcd_hba_init_crypto - Read crypto capabilities, init crypto fields in hba
 * @hba: Per adapter instance
 *
 * Return: 0 if crypto was initialized or is not supported, else a -errno value.
 */
int ufshcd_hba_init_crypto_spec(struct ufs_hba *hba,
				const struct keyslot_mgmt_ll_ops *ksm_ops)
{
	int cap_idx = 0;
	int err = 0;
	enum blk_crypto_mode_num blk_mode_num;

	/*
	 * Return 0 if crypto support isn't present/not advertised by vendor
	 * specific driver.
	 */
	if (!(hba->capabilities & MASK_CRYPTO_SUPPORT) ||
	    !(hba->caps & UFSHCD_CAP_CRYPTO))
		goto out;

	/*
	 * Crypto Capabilities should never be 0, because the
	 * config_array_ptr > 04h. So we use a 0 value to indicate that
	 * crypto init failed, and can't be enabled.
	 */
	hba->crypto_capabilities.reg_val =
			cpu_to_le32(ufshcd_readl(hba, REG_UFS_CCAP));
	hba->crypto_cfg_register =
		(u32)hba->crypto_capabilities.config_array_ptr * 0x100;
	hba->crypto_cap_array =
		devm_kcalloc(hba->dev,
			     hba->crypto_capabilities.num_crypto_cap,
			     sizeof(hba->crypto_cap_array[0]),
			     GFP_KERNEL);
	if (!hba->crypto_cap_array) {
		err = -ENOMEM;
		goto out;
	}

	err = blk_ksm_init(&hba->ksm, hba->dev, ufshcd_num_keyslots(hba));
	if (err)
		goto out_free_caps;

	hba->ksm.ksm_ll_ops = *ksm_ops;
	/* UFS only supports 8 bytes for any DUN */
	hba->ksm.max_dun_bytes_supported = 8;

	/*
	 * Store all the capabilities and fill up the keyslot manager's
	 * supported crypto modes.
	 */
	for (cap_idx = 0; cap_idx < hba->crypto_capabilities.num_crypto_cap;
	     cap_idx++) {
		hba->crypto_cap_array[cap_idx].reg_val =
			cpu_to_le32(ufshcd_readl(hba,
						 REG_UFS_CRYPTOCAP +
						 cap_idx * sizeof(__le32)));
		blk_mode_num = ufshcd_blk_crypto_mode_num_for_alg_keysize(
				hba->crypto_cap_array[cap_idx].algorithm_id,
				hba->crypto_cap_array[cap_idx].key_size);
		if (blk_mode_num != BLK_ENCRYPTION_MODE_INVALID)
			hba->ksm.crypto_modes_supported[blk_mode_num] |=
				hba->crypto_cap_array[cap_idx].sdus_mask * 512;
	}

	ufshcd_clear_all_keyslots(hba);

	return 0;

out_free_caps:
	devm_kfree(hba->dev, hba->crypto_cap_array);
out:
	/* Indicate that init failed by setting crypto_capabilities to 0 */
	hba->crypto_capabilities.reg_val = 0;
	return err;
}
EXPORT_SYMBOL(ufshcd_hba_init_crypto_spec);

void ufshcd_crypto_setup_rq_keyslot_manager_spec(struct ufs_hba *hba,
						 struct request_queue *q)
{
	if (!ufshcd_hba_is_crypto_supported(hba) || !q)
		return;

	blk_ksm_register(&hba->ksm, q);
}
EXPORT_SYMBOL(ufshcd_crypto_setup_rq_keyslot_manager_spec);

void ufshcd_crypto_destroy_keyslot_manager_spec(struct ufs_hba *hba)
{
	blk_ksm_destroy(&hba->ksm);
}
EXPORT_SYMBOL(ufshcd_crypto_destroy_keyslot_manager_spec);

int ufshcd_prepare_lrbp_crypto_spec(struct ufs_hba *hba,
				    struct scsi_cmnd *cmd,
				    struct ufshcd_lrb *lrbp)
{
	struct request *rq = cmd->request;
	struct bio_crypt_ctx *bc = rq->crypt_ctx;
	unsigned int slot_idx = blk_ksm_get_slot_idx(rq->crypt_keyslot);

	lrbp->crypto_enable = false;

	if (WARN_ON(!(hba->caps & UFSHCD_CAP_CRYPTO))) {
		/*
		 * Upper layer asked us to do inline encryption
		 * but that isn't enabled, so we fail this request.
		 */
		return -EINVAL;
	}
	if (!ufshcd_keyslot_valid(hba, slot_idx))
		return -EINVAL;

	lrbp->crypto_enable = true;
	lrbp->crypto_key_slot = slot_idx;
	lrbp->data_unit_num = bc->bc_dun[0];

	return 0;
}
EXPORT_SYMBOL(ufshcd_prepare_lrbp_crypto_spec);

/* Crypto Variant Ops Support */

void ufshcd_crypto_enable(struct ufs_hba *hba)
{
	if (hba->crypto_vops && hba->crypto_vops->enable)
		return hba->crypto_vops->enable(hba);

	return ufshcd_crypto_enable_spec(hba);
}

void ufshcd_crypto_disable(struct ufs_hba *hba)
{
	if (hba->crypto_vops && hba->crypto_vops->disable)
		return hba->crypto_vops->disable(hba);

	return ufshcd_crypto_disable_spec(hba);
}

int ufshcd_hba_init_crypto(struct ufs_hba *hba)
{
	if (hba->crypto_vops && hba->crypto_vops->hba_init_crypto)
		return hba->crypto_vops->hba_init_crypto(hba,
							 &ufshcd_ksm_ops);

	return ufshcd_hba_init_crypto_spec(hba, &ufshcd_ksm_ops);
}

void ufshcd_crypto_setup_rq_keyslot_manager(struct ufs_hba *hba,
					    struct request_queue *q)
{
	if (hba->crypto_vops && hba->crypto_vops->setup_rq_keyslot_manager)
		return hba->crypto_vops->setup_rq_keyslot_manager(hba, q);

	return ufshcd_crypto_setup_rq_keyslot_manager_spec(hba, q);
}

void ufshcd_crypto_destroy_keyslot_manager(struct ufs_hba *hba)
{
	if (hba->crypto_vops && hba->crypto_vops->destroy_keyslot_manager)
		return hba->crypto_vops->destroy_keyslot_manager(hba);

	return ufshcd_crypto_destroy_keyslot_manager_spec(hba);
}

int ufshcd_prepare_lrbp_crypto(struct ufs_hba *hba,
			       struct scsi_cmnd *cmd,
			       struct ufshcd_lrb *lrbp)
{
	if (hba->crypto_vops && hba->crypto_vops->prepare_lrbp_crypto)
		return hba->crypto_vops->prepare_lrbp_crypto(hba, cmd, lrbp);

	return ufshcd_prepare_lrbp_crypto_spec(hba, cmd, lrbp);
}

int ufshcd_map_sg_crypto(struct ufs_hba *hba, struct ufshcd_lrb *lrbp)
{
	if (hba->crypto_vops && hba->crypto_vops->map_sg_crypto)
		return hba->crypto_vops->map_sg_crypto(hba, lrbp);

	return 0;
}

int ufshcd_complete_lrbp_crypto(struct ufs_hba *hba,
				struct scsi_cmnd *cmd,
				struct ufshcd_lrb *lrbp)
{
	if (hba->crypto_vops && hba->crypto_vops->complete_lrbp_crypto)
		return hba->crypto_vops->complete_lrbp_crypto(hba, cmd, lrbp);

	return 0;
}

void ufshcd_crypto_debug(struct ufs_hba *hba)
{
	if (hba->crypto_vops && hba->crypto_vops->debug)
		hba->crypto_vops->debug(hba);
}

int ufshcd_crypto_suspend(struct ufs_hba *hba,
			  enum ufs_pm_op pm_op)
{
	if (hba->crypto_vops && hba->crypto_vops->suspend)
		return hba->crypto_vops->suspend(hba, pm_op);

	return 0;
}

int ufshcd_crypto_resume(struct ufs_hba *hba,
			 enum ufs_pm_op pm_op)
{
	if (hba->crypto_vops && hba->crypto_vops->resume)
		return hba->crypto_vops->resume(hba, pm_op);

	return 0;
}

void ufshcd_crypto_set_vops(struct ufs_hba *hba,
			    struct ufs_hba_crypto_variant_ops *crypto_vops)
{
	hba->crypto_vops = crypto_vops;
}
