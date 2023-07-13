// SPDX-License-Identifier: GPL-2.0-only
/*
 * Based on arch/arm/mm/copypage.c
 *
 * Copyright (C) 2002 Deep Blue Solutions Ltd, All Rights Reserved.
 * Copyright (C) 2012 ARM Ltd.
 */

#include <linux/bitops.h>
#include <linux/mm.h>

#include <asm/page.h>
#include <asm/cacheflush.h>
#include <asm/cpufeature.h>
#include <asm/memory_metadata.h>
#include <asm/mte.h>
#include <asm/mte_tag_storage.h>

static bool copy_page_tags_to_page(struct page *to, struct page *from)
{
	void *kfrom = page_address(from);
	void *tags;

	if (likely(page_tag_storage_reserved(to)))
		return false;

	tags = mte_allocate_tags_mem();
	if (WARN_ON(!tags))
		goto out;

	mte_save_page_tags_to_mem(kfrom, tags);

	if (WARN_ON(mte_save_page_tags_by_pfn(to, tags)))
		mte_free_tags_mem(tags);
out:
	return true;
}

void copy_highpage(struct page *to, struct page *from)
{
	void *kto = page_address(to);
	void *kfrom = page_address(from);

	copy_page(kto, kfrom);

	if (kasan_hw_tags_enabled())
		page_kasan_tag_reset(to);

	if (system_supports_mte() && page_mte_tagged(from)) {
		if (metadata_storage_enabled() &&
		    unlikely(copy_page_tags_to_page(to, from)))
			return;

		mte_copy_page_tags(kto, kfrom);
		set_page_mte_tagged(to);
	}
}
EXPORT_SYMBOL(copy_highpage);

void copy_user_highpage(struct page *to, struct page *from,
			unsigned long vaddr, struct vm_area_struct *vma)
{
	copy_highpage(to, from);
	flush_dcache_page(to);
}
EXPORT_SYMBOL_GPL(copy_user_highpage);
