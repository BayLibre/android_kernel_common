// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 Google LLC
 */
#include <linux/fs.h>

#include <uapi/linux/incrementalfs.h>

#include "data_mgmt.h"
#include "vfs.h"

/******************************************************************************
 * Define sys/fs/incrementalfs & sys/fs/incrementalfs/features
 *****************************************************************************/
#define INCFS_NODE_FEATURES "features"

struct kobject *sysfs_root;

static struct kobject *featurefs_root;

static ssize_t supported(struct kobject *kobj,
			 struct kobj_attribute *attr, char *buff)
{
	return snprintf(buff, PAGE_SIZE, "supported\n");
}

typedef ssize_t (*const attr_show)(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buff);

#define _DECLARE_FEATURE_FLAG(name)					\
	static attr_show name##_show = supported;			\
	static struct kobj_attribute name##_attr = __ATTR_RO(name)

#define DECLARE_FEATURE_FLAG(name) _DECLARE_FEATURE_FLAG(name)

DECLARE_FEATURE_FLAG(corefs);
DECLARE_FEATURE_FLAG(zstd);
DECLARE_FEATURE_FLAG(v2);

static struct attribute *attributes[] = {
	&corefs_attr.attr,
	&zstd_attr.attr,
	&v2_attr.attr,
	NULL,
};

static const struct attribute_group attr_group = {
	.attrs = attributes,
};

int __init incfs_init_sysfs(void)
{
	int res = 0;

	sysfs_root = kobject_create_and_add(INCFS_NAME, fs_kobj);
	if (!sysfs_root)
		return -ENOMEM;

	featurefs_root = kobject_create_and_add(INCFS_NODE_FEATURES,
						sysfs_root);
	if (!featurefs_root)
		return -ENOMEM;

	res = sysfs_create_group(featurefs_root, &attr_group);
	if (res) {
		kobject_put(sysfs_root);
		sysfs_root = NULL;
	}
	return res;
}

void incfs_cleanup_sysfs(void)
{
	if (featurefs_root) {
		sysfs_remove_group(featurefs_root, &attr_group);
		kobject_put(featurefs_root);
		featurefs_root = NULL;
	}

	if (sysfs_root) {
		kobject_put(sysfs_root);
		sysfs_root = NULL;
	}
}

/******************************************************************************
 * Define sys/fs/incrementalfs/<name>
 *****************************************************************************/
#define __DECLARE_STATUS_FLAG(name)					\
static ssize_t incfs_get_##name(struct kobject *kobj,			\
			 struct kobj_attribute *attr, char *buff)	\
{									\
	struct mount_info *mi = container_of(kobj, struct mount_info,	\
					     mi_sysfs_node);		\
									\
	return snprintf(buff, PAGE_SIZE, "%d\n",			\
			atomic_read(&mi->mi_##name));			\
}									\
									\
static attr_show name##_show = incfs_get_##name;			\
static struct kobj_attribute name##_attr = __ATTR_RO(name)

#define __DECLARE_STATUS_FLAG64(name)					\
static ssize_t incfs_get_##name(struct kobject *kobj,			\
			 struct kobj_attribute *attr, char *buff)	\
{									\
	struct mount_info *mi = container_of(kobj, struct mount_info,	\
					     mi_sysfs_node);		\
									\
	return snprintf(buff, PAGE_SIZE, "%lld\n",			\
			atomic64_read(&mi->mi_##name));			\
}									\
									\
static attr_show name##_show = incfs_get_##name;			\
static struct kobj_attribute name##_attr = __ATTR_RO(name)

__DECLARE_STATUS_FLAG(reads_failed_timed_out);
__DECLARE_STATUS_FLAG(reads_failed_hash_verification);
__DECLARE_STATUS_FLAG(reads_failed_other);
__DECLARE_STATUS_FLAG(reads_delayed_per_uid);
__DECLARE_STATUS_FLAG(reads_delayed_other);
__DECLARE_STATUS_FLAG64(reads_total_delay_ns);

static struct attribute *mount_attributes[] = {
	&reads_failed_timed_out_attr.attr,
	&reads_failed_hash_verification_attr.attr,
	&reads_failed_other_attr.attr,
	&reads_delayed_per_uid_attr.attr,
	&reads_delayed_other_attr.attr,
	&reads_total_delay_ns_attr.attr,
	NULL,
};

static const struct attribute_group mount_attr_group = {
	.attrs = mount_attributes,
};

static struct kobj_type incfs_kobj_ktype = {
	.sysfs_ops	= &kobj_sysfs_ops,
};

int incfs_add_sysfs_node(struct mount_info *mi, const char *name)
{
	int error;

	if (!name)
		return 0;

	kobject_init(&mi->mi_sysfs_node, &incfs_kobj_ktype);
	error = kobject_add(&mi->mi_sysfs_node, sysfs_root, "%s", name);
	if (error)
		goto out;

	error = sysfs_create_group(&mi->mi_sysfs_node, &mount_attr_group);
	if (error)
		goto out;

	mi->mi_sysfs_node_exists = true;
out:
	if (error)
		kobject_put(&mi->mi_sysfs_node);

	return error;
}

void incfs_free_sysfs_node(struct mount_info *mi)
{
	if (!mi->mi_sysfs_node_exists)
		return;

	sysfs_remove_group(&mi->mi_sysfs_node, &mount_attr_group);
	kobject_put(&mi->mi_sysfs_node);
}


