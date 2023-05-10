/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 ARM Ltd.
 */
#ifndef __ASM_MTE_TAG_STORAGE_H
#define __ASM_MTE_TAG_STORAGE_H

#include <linux/gfp.h>
#include <linux/mm_types.h>

extern void dcache_inval_tags_poc(unsigned long start, unsigned long end);

#ifdef CONFIG_ARM64_MTE_TAG_STORAGE
DECLARE_STATIC_KEY_FALSE(mte_tag_storage_enabled_key);

static inline bool mte_tag_storage_enabled(void)
{
	return static_branch_likely(&mte_tag_storage_enabled_key);
}

void mte_tag_storage_init(void);
bool alloc_can_use_tag_storage(gfp_t gfp_mask);
bool alloc_requires_tag_storage(gfp_t gfp_mask);
int reserve_tag_storage(struct page *page, int order, gfp_t gfp);
void free_tag_storage(struct page *page, int order);
bool page_tag_storage_reserved(struct page *page);
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
static inline bool alloc_requires_tag_storage(gfp_t gfp_mask)
{
	return false;
}
static inline int reserve_tag_storage(struct page *page, int order, gfp_t gfp)
{
	return 0;
}
static inline void free_tag_storage(struct page *page, int order)
{
}
static inline bool page_tag_storage_reserved(struct page *page)
{
	return true;
}
#endif /* CONFIG_ARM64_MTE_TAG_STORAGE */

#endif /* __ASM_MTE_TAG_STORAGE_H  */
