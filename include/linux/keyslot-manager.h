// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Google LLC
 */

#ifndef __LINUX_KEYSLOT_MANAGER_H
#define __LINUX_KEYSLOT_MANAGER_H

#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/types.h>

enum keyslot_manager_algs {
	UNSUPPORTED_ALG,
	AES_XTS,
	BITLOCKER_AES_CBC,
	AES_ECB,
	ESSIV_AES_CBC,
};

struct keyslot_mgmt_ll_ops {
	int (*keyslot_program)(void *ll_priv_data, const u8 *key,
				unsigned int dataunit_size,
				/* alg_type as returned by crypto alg find */
				unsigned int alg_type,
				unsigned int slot);
	/* Returns 0 on success, -errno otherwise */
	int (*keyslot_evict)(void *ll_priv_data, unsigned int slot);
	int (*crypto_alg_find)(void *ll_priv_data, size_t key_size_bytes,
				enum keyslot_manager_algs alg,
				unsigned int dataunit_size);
	/**
	 * Returns the slot number that matches the key,
	 * or -ENOKEY if no match found, or negative on error
	 */
	int (*keyslot_find)(void *ll_priv_data, const u8 *key,
			    unsigned int dataunit_size,
			    unsigned int alg_type);
	size_t max_ident_size;
};

struct keyslot_manager {
	unsigned int num_slots;
	atomic_t *slot_refs;
	atomic_t num_free_slots;
	struct keyslot_mgmt_ll_ops ksm_ll_ops;
	void *ll_priv_data;
	struct mutex lock;
	wait_queue_head_t wait_queue;
};

#ifdef CONFIG_BLK_KEYSLOT_MANAGER
extern struct keyslot_manager *keyslot_manager_create(unsigned int num_slots,
				const struct keyslot_mgmt_ll_ops *ksm_ops,
				void *ll_priv_data);

extern int keyslot_manager_get_slot(struct keyslot_manager *ksm,
				    const u8 *key, size_t key_size_bytes,
				    enum keyslot_manager_algs alg,
				    unsigned int dataunit_size);

extern int keyslot_manager_release_slot(struct keyslot_manager *ksm,
					unsigned int slot);

extern int keyslot_manager_evict_key(struct keyslot_manager *ksm,
				     const u8 *key,
				     size_t key_size_bytes,
				     enum keyslot_manager_algs alg,
				     unsigned int dataunit_size);

extern void keyslot_manager_destroy(struct keyslot_manager *ksm);

#else /* CONFIG_BLK_KEYSLOT_MANAGER */

static inline struct keyslot_manager *keyslot_manager_create(
				unsigned int num_slots,
				const struct keyslot_mgmt_ll_ops *ksm_ops,
				void *ll_priv_data)
{
	return NULL;
}

static inline int keyslot_manager_get_slot(struct keyslot_manager *ksm,
				    const u8 *key, size_t key_size_bytes,
				    enum keyslot_manager_algs alg,
				    unsigned int dataunit_size)
{
	return -1;
}

static inline int keyslot_manager_release_slot(struct keyslot_manager *ksm,
					unsigned int slot)
{
	return -1;
}

static inline int keyslot_manager_evict_key(struct keyslot_manager *ksm,
				     const u8 *key,
				     size_t key_size_bytes,
				     enum keyslot_manager_algs alg,
				     unsigned int dataunit_size)
{
	return -1;
}

static inline void keyslot_manager_destroy(struct keyslot_manager *ksm)
{ }

static inline size_t keyslot_manager_max_ident_size(
	struct keyslot_manager *ksm)
{
	return 0;
}

#endif /* CONFIG_BLK_KEYSLOT_MANAGER */

#endif /* __LINUX_KEYSLOT_MANAGER_H */
