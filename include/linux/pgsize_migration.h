/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PAGE_SIZE_MIGRATION_H
#define _LINUX_PAGE_SIZE_MIGRATION_H

/*
 * include/linux/pgsize_migration.h
 *
 * Page Size Migration
 *
 * Copyright (c) 2024, Google LLC.
 * Author: Kalesh Singh <kaleshsingh@goole.com>
 *
 * This file contains the APIs for mitigations to ensure
 * app compatibility during the transition from 4kB to 16kB
 * page size in Android.
 */

#include <linux/mm.h>
#include <linux/string.h>

/*
 * 4 high bits of vm_flags [62,59] (63 is used for page size emulation in x86_64)
 * are used to represent ELF segment padding up to 60kB, which is sufficient for
 * ELFs of both 16kB and 64kB segment alignment (p_align).
 *
 * The representation is illustrated below.
 *
 *                    62        61        60        59
 *                _________ _________ _________ _________
 *               |  Bit 2  |  Bit 1  |  Bit 2  |  Bit 1  |
 *               | of 16kB | of 16kB | of  4kB | of  4kB |
 *               |  chunks |  chunks |  chunks |  chunks |
 *               |_________|_________|_________|_________|
 */

#define VM_PAD_16KB_BIT2	(_AC(1,ULL) << 62)
#define VM_PAD_16KB_BIT1	(_AC(1,ULL) << 61)
#define VM_PAD_4KB_BIT2		(_AC(1,ULL) << 60)
#define VM_PAD_4KB_BIT1		(_AC(1,ULL) << 59)
#define VM_PAD_BITS		(VM_PAD_16KB_BIT2|VM_PAD_16KB_BIT1|VM_PAD_4KB_BIT2|VM_PAD_4KB_BIT1)
#define VM_TOTAL_PAD_PAGES 	15

extern bool pgsize_migration_enabled;

static inline void vma_set_pad_pages(struct vm_area_struct *vma,
				     unsigned long nr_pages)
{
	vm_flags_t flags = 0;

	if (nr_pages & 1UL)
		flags |=  VM_PAD_4KB_BIT1;
	if (nr_pages & 2UL)
		flags |=  VM_PAD_4KB_BIT2;
	if (nr_pages & 4UL)
		flags |=  VM_PAD_16KB_BIT1;
	if (nr_pages & 8UL)
		flags |=  VM_PAD_16KB_BIT2;

	vma->vm_flags |= flags;
}

static inline unsigned long vma_pad_pages(struct vm_area_struct *vma)
{
	unsigned long nr_pages = 0;

	if (vma->vm_flags & VM_PAD_4KB_BIT1)
		nr_pages |= 1UL;
	if (vma->vm_flags & VM_PAD_4KB_BIT2)
		nr_pages |= 2UL;
	if (vma->vm_flags & VM_PAD_16KB_BIT1)
		nr_pages |= 4UL;
	if (vma->vm_flags & VM_PAD_16KB_BIT2)
		nr_pages |= 8UL;

	return nr_pages;
}

static inline unsigned long vma_data_pages(struct vm_area_struct *vma)
{
	return vma_pages(vma) - vma_pad_pages(vma);
}

/*
 * Saves the number of ELF padding pages at in vm_flags
 */
static inline void madvise_vma_pad_pages(struct vm_area_struct *vma,
				      unsigned long start, unsigned long end)
{
	unsigned long nr_pad_pages;
	const unsigned char *name;
	size_t len;

	if (!pgsize_migration_enabled)
		return;

	/* Only handle this for file backed VMAs */
	if (!vma->vm_file || !vma->vm_ops || vma->vm_ops->fault != filemap_fault)
		return;

	name = vma->vm_file->f_path.dentry->d_name.name;
	len = strlen(name);

	/* Limit this to only shared libraries (*.so) */
	if (len <= 3 || strncmp(name + len - 3, ".so", 3))
		return;

	/*
	 * If the madvise range is it at the end of the file save the number of
	 * pages in vm_flags (only need 4 bits are needed for 16kB aligned ELFs).
	 */
	if (start <= vma->vm_start || end != vma->vm_end)
		return;

	nr_pad_pages = (end - start) >> PAGE_SHIFT;

	if (!nr_pad_pages || nr_pad_pages > VM_TOTAL_PAD_PAGES)
		return;

	vma_set_pad_pages(vma, nr_pad_pages);
}
#endif /* _LINUX_PAGE_SIZE_MIGRATION_H */
