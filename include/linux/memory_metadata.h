/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_MEMORY_METADATA_H
#define __LINUX_MEMORY_METADATA_H

#include <linux/gfp.h>
#include <linux/mm_types.h>

extern unsigned long totalmetadata_pages;

#ifdef CONFIG_MEMORY_METADATA

#include <asm/memory_metadata.h>

#define metadata_storage_enabled()		arch_metadata_storage_enabled()

#define alloc_can_use_metadata_pages(gfp_mask)	arch_alloc_can_use_metadata_pages(gfp_mask)
#define page_has_metadata(page)			arch_page_has_metadata(page)
#define alloc_requires_metadata(gfp_mask)	arch_alloc_requires_metadata(gfp_mask)
#define reserve_metadata_storage(page, order, gfp) arch_reserve_metadata_storage(page, order, gfp)
#define free_metadata_storage(page, order) 	arch_free_metadata_storage(page, order)

#else
static inline bool metadata_storage_enabled(void)
{
	return false;
}
static inline bool alloc_can_use_metadata_pages(gfp_t gfp_mask)
{
	return false;
}
static inline bool page_has_metadata(struct page *page)
{
	return false;
}
static inline bool alloc_requires_metadata(gfp_t gfp_mask)
{
	return false;
}
static inline int reserve_metadata_storage(struct page *page, int order, gfp_t gfp)
{
	return 0;
}
static inline void free_metadata_storage(struct page *page, int order)
{
}
#endif /* CONFIG_MEMORY_METADATA */

#endif /* __LINUX_MEMORY_METADATA_H */
