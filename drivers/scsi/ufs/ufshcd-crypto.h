// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Google LLC
 */

#ifndef _UFSHCD_CRYPTO_H
#define _UFSHCD_CRYPTO_H

#ifdef CONFIG_SCSI_UFS_CRYPTO
#include "ufshcd.h"
#include "ufshci.h"

bool ufshcd_keyslot_valid(struct ufs_hba *hba, unsigned int slot);

bool ufshcd_hba_is_crypto_supported(struct ufs_hba *hba);

bool ufshcd_is_crypto_enabled(struct ufs_hba *hba);

int ufshcd_prepare_lrbp_crypto_spec(struct ufs_hba *hba,
				    struct scsi_cmnd *cmd,
				    struct ufshcd_lrb *lrbp);

int ufshcd_crypto_enable_spec(struct ufs_hba *hba);

int ufshcd_crypto_disable_spec(struct ufs_hba *hba);

int ufshcd_hba_init_crypto_spec(struct ufs_hba *hba);

struct keyslot_mgmt_ll_ops;
int ufshcd_crypto_setup_rq_keyslot_manager_ksm_spec(struct ufs_hba *hba,
				struct request_queue *q,
				const struct keyslot_mgmt_ll_ops *ksm_ops);

int ufshcd_crypto_setup_rq_keyslot_manager_spec(struct ufs_hba *hba,
						struct request_queue *q);

int ufshcd_crypto_destroy_rq_keyslot_manager_spec(struct request_queue *q);

/* Crypto Variant Ops Support */

int ufshcd_crypto_enable(struct ufs_hba *hba);

int ufshcd_crypto_disable(struct ufs_hba *hba);

int ufshcd_hba_init_crypto(struct ufs_hba *hba);

int ufshcd_crypto_setup_rq_keyslot_manager(struct ufs_hba *hba,
					   struct request_queue *q);

int ufshcd_crypto_destroy_rq_keyslot_manager(struct ufs_hba *hba,
					     struct request_queue *q);

int ufshcd_prepare_lrbp_crypto(struct ufs_hba *hba,
			       struct scsi_cmnd *cmd,
			       struct ufshcd_lrb *lrbp);

int ufshcd_complete_lrbp_crypto(struct ufs_hba *hba,
				struct scsi_cmnd *cmd,
				struct ufshcd_lrb *lrbp);
#else /* CONFIG_UFS_CRYPTO */

static inline bool ufshcd_keyslot_valid(struct ufs_hba *hba,
					unsigned int slot)
{
	return false;
}

static inline bool ufshcd_hba_is_crypto_supported(struct ufs_hba *hba)
{
	return false;
}

static inline bool ufshcd_is_crypto_enabled(struct ufs_hba *hba)
{
	return false;
}

static inline int ufshcd_crypto_set_enable_slot(struct ufs_hba *hba,
				  unsigned int slot,
				  bool enable)
{
	return -1;
}

static inline int ufshcd_crypto_enable(struct ufs_hba *hba)
{
	return -1;
}

static inline int ufshcd_crypto_disable(struct ufs_hba *hba)
{
	return -1;
}

static inline int ufshcd_hba_init_crypto(struct ufs_hba *hba)
{
	return -1;
}

static inline int ufshcd_crypto_setup_rq_keyslot_manager(
					struct ufs_hba *hba,
					struct request_queue *q)
{
	return -1;
}

static inline int ufshcd_crypto_destroy_rq_keyslot_manager(struct ufs_hba *hba,
				struct request_queue *q)
{
	return -1;
}

static inline int ufshcd_prepare_lrbp_crypto(struct ufs_hba *hba,
					     struct scsi_cmnd *cmd,
					     struct ufshcd_lrb *lrbp)
{
	lrbp->crypto_enable = false;
	return 0;
}

int ufshcd_complete_lrbp_crypto(struct ufs_hba *hba,
				struct scsi_cmnd *cmd,
				struct ufshcd_lrb *lrbp)
{
	return 0;
}

#endif /* CONFIG_SCSI_UFS_CRYPTO */

#endif /* _UFSHCD_CRYPTO_H */
