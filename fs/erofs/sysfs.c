// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Copyright (C) 2022 Google, Inc.
 *
 * Author:
 *	Kelvin Zhang <zhangkelvin@google.com>
 */

#include <linux/compiler.h>
#include <linux/kobject.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/unicode.h>
#include <linux/ioprio.h>
#include <linux/sysfs.h>

static struct kobject *erofs_root;
static struct kobject *erofs_feat;

static ssize_t erofs_attr_show(struct kobject *kobj, struct attribute *attr,
			      char *buf)
{
	// By default all features listed are supported.
	return snprintf(buf, PAGE_SIZE, "supported\n");
}

static ssize_t erofs_attr_store(struct kobject *kobj, struct attribute *attr,
			       const char *buf, size_t len)
{
	// We don't support turning these features off
	return 0;
}

static const struct sysfs_ops erofs_attr_ops = {
	.show = erofs_attr_show,
	.store = erofs_attr_store,
};

#define EROFS_ATTR(_name)                                              \
	static struct attribute erofs_attr_##_name = {                     \
		.name = __stringify(_name), .mode = 0444                       \
	}

#define ATTR_LIST(name) (&erofs_attr_##name)

EROFS_ATTR(sb_csum);
EROFS_ATTR(0padding);
EROFS_ATTR(big_pcluster);
EROFS_ATTR(chunked_file);
EROFS_ATTR(device_table);

static struct attribute *erofs_feat_attrs[] = {
	ATTR_LIST(sb_csum),
	ATTR_LIST(0padding),
	ATTR_LIST(big_pcluster),
	ATTR_LIST(chunked_file),
	ATTR_LIST(device_table),
	NULL,
};

ATTRIBUTE_GROUPS(erofs_feat);

static struct kobj_type erofs_feat_ktype = {
	.default_groups = erofs_feat_groups,
	.sysfs_ops = &erofs_attr_ops,
	.release = (void (*)(struct kobject *))kfree,
};

static const char proc_dirname[] = "fs/erofs";
static struct proc_dir_entry *erofs_proc_root;

int erofs_init_sysfs(void)
{
	int ret = 0;

	erofs_root = kobject_create_and_add("erofs", fs_kobj);
	if (!erofs_root)
		return -ENOMEM;
	erofs_feat = kzalloc(sizeof(*erofs_feat), GFP_KERNEL);
	if (!erofs_feat) {
		ret = -ENOMEM;
		goto root_err;
	}
	ret = kobject_init_and_add(erofs_feat, &erofs_feat_ktype, erofs_root,
				   "features");
	if (ret)
		goto feat_err;
	erofs_proc_root = proc_mkdir(proc_dirname, NULL);
	return ret;

feat_err:
	kobject_put(erofs_feat);
	erofs_feat = NULL;
root_err:
	kobject_put(erofs_root);
	erofs_root = NULL;
	return ret;
}


void erofs_exit_sysfs(void)
{
	kobject_put(erofs_feat);
	erofs_feat = NULL;
	kobject_put(erofs_root);
	erofs_root = NULL;
	remove_proc_entry(proc_dirname, NULL);
	erofs_proc_root = NULL;
}

