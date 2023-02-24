/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_MEMORY_METADATA_H
#define __LINUX_MEMORY_METADATA_H

#include <linux/gfp.h>

#ifdef CONFIG_MEMORY_METADATA

#include <asm/memory_metadata.h>

#define metadata_storage_enabled()		arch_metadata_storage_enabled()

#define alloc_can_use_metadata_pages(gfp_mask)	arch_alloc_can_use_metadata_pages(gfp_mask)
#else
static inline bool metadata_storage_enabled(void)
{
	return false;
}
static inline bool alloc_can_use_metadata_pages(gfp_t gfp_mask)
{
	return false;
}
#endif /* CONFIG_MEMORY_METADATA */

#endif /* __LINUX_MEMORY_METADATA_H */
