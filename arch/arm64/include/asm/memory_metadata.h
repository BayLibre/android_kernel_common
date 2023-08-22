/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 ARM Ltd.
 */
#ifndef __ASM_MEMORY_METADATA_H
#define __ASM_MEMORY_METADATA_H

#include <linux/mm.h>

#include <asm-generic/memory_metadata.h>

#include <asm/mte.h>

#ifdef CONFIG_MEMORY_METADATA
DECLARE_STATIC_KEY_FALSE(metadata_storage_enabled_key);

static inline bool metadata_storage_enabled(void)
{
	return static_branch_likely(&metadata_storage_enabled_key);
}

static inline bool alloc_can_use_metadata_pages(gfp_t gfp_mask)
{
	return !(gfp_mask & __GFP_TAGGED);
}

static inline bool alloc_requires_metadata(gfp_t gfp_mask)
{
	return gfp_mask & __GFP_TAGGED;
}

#define page_has_metadata(page)			page_mte_tagged(page)

static inline bool folio_has_metadata(struct folio *folio)
{
	return page_has_metadata(&folio->page);
}

static inline bool vma_has_metadata(struct vm_area_struct *vma)
{
	return vma && (vma->vm_flags & VM_MTE);
}

int reserve_metadata_storage(struct page *page, int order, gfp_t gfp_mask);
void free_metadata_storage(struct page *page, int order);
#endif /* CONFIG_MEMORY_METADATA */

#endif /* __ASM_MEMORY_METADATA_H  */
