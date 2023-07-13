/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 ARM Ltd.
 */
#ifndef __ASM_MTE_TAG_STORAGE_H
#define __ASM_MTE_TAG_STORAGE_H

#include <linux/mm_types.h>

extern void dcache_inval_tags_poc(unsigned long start, unsigned long end);

#ifdef CONFIG_ARM64_MTE_TAG_STORAGE
void mte_tag_storage_init(void);
bool page_tag_storage_reserved(struct page *page);

void *mte_erase_page_tags_by_pfn(struct page *page);
int mte_save_page_tags_by_pfn(struct page *page, void *tags);
#else
static inline void mte_tag_storage_init(void)
{
}
static inline bool page_tag_storage_reserved(struct page *page)
{
	return true;
}
static inline void *mte_erase_page_tags_by_pfn(struct page *page)
{
	return NULL;
}
static inline int mte_save_page_tags_by_pfn(struct page *page, void *tags)
{
	return 0;
}
#endif /* CONFIG_ARM64_MTE_TAG_STORAGE */

#endif /* __ASM_MTE_TAG_STORAGE_H  */
