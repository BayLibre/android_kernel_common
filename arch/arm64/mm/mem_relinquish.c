/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/mem_relinquish.h>

#include <asm/hypervisor.h>

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
