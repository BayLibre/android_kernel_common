/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 Google LLC
 * Author: Keir Fraser <keirf@google.com>
 */

#include <linux/arm-smccc.h>
#include <linux/mem_relinquish.h>
#include <linux/memory.h>
#include <linux/mm.h>
#include <linux/types.h>

#include <asm/hypervisor.h>

void page_relinquish(struct page *page)
{
	if (hyp_ops.page_relinquish)
		hyp_ops.page_relinquish(page);
}
EXPORT_SYMBOL_GPL(page_relinquish);
