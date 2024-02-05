/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <linux/coco-guest.h>
#include <linux/export.h>
#include <linux/mem_relinquish.h>

struct hypervisor_ops hyp_ops;

#ifdef CONFIG_MEMORY_RELINQUISH
bool page_relinquish_disallowed(void)
{
	if (hyp_ops.page_relinquish_disallowed)
		return hyp_ops.page_relinquish_disallowed();

	return false;
}
EXPORT_SYMBOL_GPL(page_relinquish_disallowed);

void page_relinquish(struct page *page)
{
	if (hyp_ops.page_relinquish)
		hyp_ops.page_relinquish(page);
}
EXPORT_SYMBOL_GPL(page_relinquish);
#endif
