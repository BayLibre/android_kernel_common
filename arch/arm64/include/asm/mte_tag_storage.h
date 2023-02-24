/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 ARM Ltd.
 */
#ifndef __ASM_MTE_TAG_STORAGE_H
#define __ASM_MTE_TAG_STORAGE_H

#include <linux/gfp.h>

#ifdef CONFIG_ARM64_MTE_TAG_STORAGE
DECLARE_STATIC_KEY_FALSE(mte_tag_storage_enabled_key);

static inline bool mte_tag_storage_enabled(void)
{
	return static_branch_likely(&mte_tag_storage_enabled_key);
}

void mte_tag_storage_init(void);
bool alloc_can_use_tag_storage(gfp_t gfp_mask);
#else
static inline bool mte_tag_storage_enabled(void)
{
	return false;
}
static inline void mte_tag_storage_init(void)
{
}
static inline bool alloc_can_use_tag_storage(gfp_t gfp_mask)
{
	return false;
}
#endif /* CONFIG_ARM64_MTE_TAG_STORAGE */

#endif /* __ASM_MTE_TAG_STORAGE_H  */
