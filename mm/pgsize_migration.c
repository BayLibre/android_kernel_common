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
DEFINE_PER_CPU(struct vm_area_struct, pad_vma);

static const char *pad_name = "[page size compat]";

const char *vma_pad_name(struct vm_area_struct *vma)
{
	struct vm_area_struct *pad = this_cpu_ptr(&pad_vma);

	if (!pgsize_migration_enabled || vma != pad)
		return NULL;

	return pad_name;
}

void show_map_vma_pad(struct vm_area_struct *vma,
				    show_map_vma_fn func,
				    struct seq_file *m)
{
	struct vm_area_struct *pad = this_cpu_ptr(&pad_vma);

	if (!pgsize_migration_enabled)
		return;

	if (!(vma->vm_flags & VM_PAD_BITS))
		return;

	*pad = *vma;
	pad->vm_flags = vma->vm_flags & ~(VM_PAD_BITS|VM_READ|VM_WRITE|VM_EXEC);
	pad->vm_file = NULL;
	pad->vm_start = vma->vm_end - (vma_pad_pages(vma) << PAGE_SHIFT);
	func(m, pad);
}
#endif
