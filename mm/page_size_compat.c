// SPDX-License-Identifier: GPL-2.0
/*
 * Page Size Emulation
 *
 * Copyright (c) 2024, Google LLC.
 * Author: Kalesh Singh <kaleshsingh@goole.com>
 */

#include <linux/init.h>
#include <linux/jump_label.h>
#include <linux/kstrtox.h>
#include <linux/page_size_compat.h>

DEFINE_STATIC_KEY_FALSE(page_shift_compat_enabled);
static int page_shift_compat = PAGE_SHIFT;
static int max_page_shift_compat = 16;  /* Max of 64KB */

static int __init early_page_shift_compat(char *buf)
{
	int ret;

	ret = kstrtoint(buf, 10, &page_shift_compat);
	if (ret)
		return ret;

	if (page_shift_compat > PAGE_SHIFT &&
		page_shift_compat <= max_page_shift_compat)
		static_branch_enable(&page_shift_compat_enabled);

	return 0;
}
early_param("androidboot.page_shift", early_page_shift_compat);

unsigned __page_shift(void)
{
	if (static_branch_unlikely(&page_shift_compat_enabled))
		return page_shift_compat;
	else
		return PAGE_SHIFT;
}
