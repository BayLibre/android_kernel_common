/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 ARM Ltd.
 */
#ifndef __ASM_MEMORY_METADATA_H
#define __ASM_MEMORY_METADATA_H

#include <asm/mte_tag_storage.h>

#define arch_metadata_storage_enabled()			mte_tag_storage_enabled()

#define arch_alloc_can_use_metadata_pages(gfp_mask)	alloc_can_use_tag_storage(gfp_mask)

#endif /* __ASM_MTE_TAG_STORAGE_H  */
