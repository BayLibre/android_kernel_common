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
#include <linux/mm.h>
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

	if (PAGE_SHIFT == 12 && 	/* Only supported on 4KB kernel */
		page_shift_compat > PAGE_SHIFT &&
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

bool __log_alignment(const char* func, unsigned long addr, bool is_aligned)
{
	if (is_aligned)
		return true;

	if (!static_branch_unlikely(&page_shift_compat_enabled))
		return false;

	pgcompat_err("%s: addr (0x%08lx) not page aligned", func, addr);

	return false;
}

#define __MMAP_RND_BITS(x)      (x - (__PAGE_SHIFT - PAGE_SHIFT))

static int __init init_mmap_rnd_bits(void)
{
	if (!static_branch_unlikely(&page_shift_compat_enabled))
		return 0;

#ifdef CONFIG_HAVE_ARCH_MMAP_RND_BITS
	mmap_rnd_bits_min = __MMAP_RND_BITS(CONFIG_ARCH_MMAP_RND_BITS_MIN);
	mmap_rnd_bits_max = __MMAP_RND_BITS(CONFIG_ARCH_MMAP_RND_BITS_MAX);
	mmap_rnd_bits = __MMAP_RND_BITS(CONFIG_ARCH_MMAP_RND_BITS);
#endif

	return 0;
}
core_initcall(init_mmap_rnd_bits);
