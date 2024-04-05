// SPDX-License-Identifier: GPL-2.0
/*
 * Page Size Migration
 *
 * This file contains the core logic of mitigations to ensure
 * app compatibility during the transition from 4kB to 16kB
 * page size in Android.
 *
 * Copyright (c) 2024, Google LLC.
 * Author: Kalesh Singh <kaleshsingh@goole.com>
 */

#include <linux/pgsize_migration.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>

bool pgsize_migration_enabled = PAGE_SIZE == SZ_4K;

static ssize_t show_pgsize_migration_enabled(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", pgsize_migration_enabled);
}

static ssize_t store_pgsize_migration_enabled(struct kobject *kobj,
				    struct kobj_attribute *attr,
				    const char *buf, size_t n)
{
	unsigned long val;

	/* Migration is only applicable to 4kB kernels */
	if (PAGE_SIZE != SZ_4K)
		return n;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	if (val > 1)
		return -EINVAL;

	pgsize_migration_enabled = !!val;
	return n;
}

static struct kobj_attribute pgsize_migration_enabled_attr = __ATTR(
	enabled,
	0644,
	show_pgsize_migration_enabled,
	store_pgsize_migration_enabled
);

static struct attribute *pgsize_migration_attrs[] = {
	&pgsize_migration_enabled_attr.attr,
	NULL
};

static struct attribute_group pgsize_migration_attr_group = {
	.name = "pgsize_migration",
	.attrs = pgsize_migration_attrs,
};

static int __init init_pgsize_migration(void)
{
	if (sysfs_create_group(mm_kobj, &pgsize_migration_attr_group))
		pr_err("pgsize_migration: failed to create sysfs group\n");

	return 0;
};
late_initcall(init_pgsize_migration);

#if PAGE_SIZE == SZ_4K
/*
 * Saves the number of padding pages for an ELF segment mapping
 * in vm_flags.
 */
void madvise_vma_pad_pages(struct vm_area_struct *vma,
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
#endif /* PAGE_SIZE == SZ_4K */
