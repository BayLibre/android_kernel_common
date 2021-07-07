// SPDX-License-Identifier: GPL-2.0-only
/*
 * DMA-BUF sysfs statistics.
 *
 * Copyright (C) 2021 Google LLC.
 */

#include <linux/dma-buf.h>
#include <linux/dma-resv.h>
#include <linux/kobject.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#include "dma-buf-sysfs-stats.h"

#define to_dma_buf_entry_from_kobj(x) container_of(x, struct dma_buf_sysfs_entry, kobj)

struct dma_buf_stats_attribute {
	struct attribute attr;
	ssize_t (*show)(struct dma_buf *dmabuf,
			struct dma_buf_stats_attribute *attr, char *buf);
};
#define to_dma_buf_stats_attr(x) container_of(x, struct dma_buf_stats_attribute, attr)

struct attachment_work_info {
	struct list_head list;
	struct dma_buf_attach_sysfs_entry *sysfs_entry;
	struct device *attachment_dev;
	union {
		struct dma_buf *buf;
		unsigned long uid;
	};
	bool is_setup_work;
};

static LIST_HEAD(attachment_work_list);
static DEFINE_SPINLOCK(work_lock);
static struct work_struct attachment_work_struct;

static ssize_t dma_buf_stats_attribute_show(struct kobject *kobj,
					    struct attribute *attr,
					    char *buf)
{
	struct dma_buf_stats_attribute *attribute;
	struct dma_buf_sysfs_entry *sysfs_entry;
	struct dma_buf *dmabuf;

	attribute = to_dma_buf_stats_attr(attr);
	sysfs_entry = to_dma_buf_entry_from_kobj(kobj);
	dmabuf = sysfs_entry->dmabuf;

	if (!dmabuf || !attribute->show)
		return -EIO;

	return attribute->show(dmabuf, attribute, buf);
}

static const struct sysfs_ops dma_buf_stats_sysfs_ops = {
	.show = dma_buf_stats_attribute_show,
};

static ssize_t exporter_name_show(struct dma_buf *dmabuf,
				  struct dma_buf_stats_attribute *attr,
				  char *buf)
{
	return sysfs_emit(buf, "%s\n", dmabuf->exp_name);
}

static ssize_t size_show(struct dma_buf *dmabuf,
			 struct dma_buf_stats_attribute *attr,
			 char *buf)
{
	return sysfs_emit(buf, "%zu\n", dmabuf->size);
}

static struct dma_buf_stats_attribute exporter_name_attribute =
	__ATTR_RO(exporter_name);
static struct dma_buf_stats_attribute size_attribute = __ATTR_RO(size);

static struct attribute *dma_buf_stats_default_attrs[] = {
	&exporter_name_attribute.attr,
	&size_attribute.attr,
	NULL,
};
ATTRIBUTE_GROUPS(dma_buf_stats_default);

static void dma_buf_sysfs_release(struct kobject *kobj)
{
	struct dma_buf_sysfs_entry *sysfs_entry;

	sysfs_entry = to_dma_buf_entry_from_kobj(kobj);
	kfree(sysfs_entry);
}

static struct kobj_type dma_buf_ktype = {
	.sysfs_ops = &dma_buf_stats_sysfs_ops,
	.release = dma_buf_sysfs_release,
	.default_groups = dma_buf_stats_default_groups,
};

#define to_dma_buf_attach_entry_from_kobj(x) container_of(x, struct dma_buf_attach_sysfs_entry, kobj)

struct dma_buf_attach_stats_attribute {
	struct attribute attr;
	ssize_t (*show)(struct dma_buf_attach_sysfs_entry *sysfs_entry,
			struct dma_buf_attach_stats_attribute *attr, char *buf);
};
#define to_dma_buf_attach_stats_attr(x) container_of(x, struct dma_buf_attach_stats_attribute, attr)

static ssize_t dma_buf_attach_stats_attribute_show(struct kobject *kobj,
						   struct attribute *attr,
						   char *buf)
{
	struct dma_buf_attach_stats_attribute *attribute;
	struct dma_buf_attach_sysfs_entry *sysfs_entry;

	attribute = to_dma_buf_attach_stats_attr(attr);
	sysfs_entry = to_dma_buf_attach_entry_from_kobj(kobj);

	if (!attribute->show)
		return -EIO;

	return attribute->show(sysfs_entry, attribute, buf);
}

static const struct sysfs_ops dma_buf_attach_stats_sysfs_ops = {
	.show = dma_buf_attach_stats_attribute_show,
};

static ssize_t map_counter_show(struct dma_buf_attach_sysfs_entry *sysfs_entry,
				struct dma_buf_attach_stats_attribute *attr,
				char *buf)
{
	return sysfs_emit(buf, "%u\n", sysfs_entry->map_counter);
}

static struct dma_buf_attach_stats_attribute map_counter_attribute =
	__ATTR_RO(map_counter);

static struct attribute *dma_buf_attach_stats_default_attrs[] = {
	&map_counter_attribute.attr,
	NULL,
};
ATTRIBUTE_GROUPS(dma_buf_attach_stats_default);

static void dma_buf_attach_sysfs_release(struct kobject *kobj)
{
	struct dma_buf_attach_sysfs_entry *sysfs_entry;

	sysfs_entry = to_dma_buf_attach_entry_from_kobj(kobj);
	kfree(sysfs_entry);
}

static struct kobj_type dma_buf_attach_ktype = {
	.sysfs_ops = &dma_buf_attach_stats_sysfs_ops,
	.release = dma_buf_attach_sysfs_release,
	.default_groups = dma_buf_attach_stats_default_groups,
};

static void dma_buf_attach_stats_do_teardown_work(struct dma_buf_attach_sysfs_entry *sysfs_entry,
						  struct device *attachment_dev,
						  struct dma_buf *buf)
{
	if (!sysfs_entry->is_valid)
		goto teardown_invalid_entry;

	sysfs_delete_link(&sysfs_entry->kobj, &attachment_dev->kobj, "device");

	kobject_del(&sysfs_entry->kobj);

teardown_invalid_entry:
	kobject_put(&sysfs_entry->kobj);
	dma_buf_put(buf);
	put_device(attachment_dev);
}

static void dma_buf_attach_stats_do_setup_work(struct dma_buf_attach_sysfs_entry *sysfs_entry,
					       struct device *attachment_dev,
					       unsigned int uid)
{
	int ret;

	ret = kobject_add(&sysfs_entry->kobj, NULL, "%u", uid);
	if (ret)
		return;

	ret = sysfs_create_link(&sysfs_entry->kobj, &attachment_dev->kobj,
				"device");
	if (ret)
		goto link_err;

	sysfs_entry->is_valid = true;
	return;

link_err:
	kobject_del(&sysfs_entry->kobj);
}

static void process_attachment_workqueue(struct work_struct *work)
{
	struct attachment_work_info *attachment_work, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&work_lock, flags);
	list_for_each_entry_safe(attachment_work, tmp,
				 &attachment_work_list, list) {
		list_del(&attachment_work->list);
		spin_unlock_irqrestore(&work_lock, flags);

		if (attachment_work->is_setup_work)
			dma_buf_attach_stats_do_setup_work(attachment_work->sysfs_entry,
							   attachment_work->attachment_dev,
							   attachment_work->uid);
		else
			dma_buf_attach_stats_do_teardown_work(attachment_work->sysfs_entry,
							      attachment_work->attachment_dev,
							      attachment_work->buf);

		kfree(attachment_work);
		spin_lock_irqsave(&work_lock, flags);
	}
	spin_unlock_irqrestore(&work_lock, flags);
}

void dma_buf_attach_stats_teardown(struct dma_buf_attachment *attach)
{
	struct attachment_work_info *attachment_work;
	unsigned long flags;

	attachment_work = kmalloc(sizeof(struct dma_buf_attach_sysfs_entry),
				  GFP_KERNEL);
	if (!attachment_work)
		return;

	attachment_work->is_setup_work = false;
	attachment_work->sysfs_entry = attach->sysfs_entry;
	attachment_work->attachment_dev = attach->dev;
	attachment_work->buf = attach->dmabuf;

	spin_lock_irqsave(&work_lock, flags);
	list_add(&attachment_work->list, &attachment_work_list);
	spin_unlock_irqrestore(&work_lock, flags);

	queue_work(system_wq, &attachment_work_struct);
}

int dma_buf_attach_stats_setup(struct dma_buf_attachment *attach,
			       unsigned int uid)
{
	struct dma_buf *dmabuf;
	struct dma_buf_attach_sysfs_entry *sysfs_entry;
	struct attachment_work_info *attachment_work;
	unsigned long flags;
	int ret = 0;

	if (!attach)
		return -EINVAL;

	dmabuf = attach->dmabuf;

	sysfs_entry = kzalloc(sizeof(struct dma_buf_attach_sysfs_entry),
			      GFP_KERNEL);
	if (!sysfs_entry)
		return -ENOMEM;

	attachment_work = kmalloc(sizeof(struct dma_buf_attach_sysfs_entry),
				  GFP_KERNEL);
	if (!attachment_work) {
		ret = -ENOMEM;
		goto free_attachment_work;
	}

	attachment_work->is_setup_work = true;
	attachment_work->sysfs_entry = sysfs_entry;
	attachment_work->attachment_dev = attach->dev;
	attachment_work->uid = uid;

	/*
	 * The corresponding puts for dmabuf and attach->dev are in
	 * dma_buf_attach_stats_do_teardown_work()
	 */
	get_dma_buf(dmabuf);
	get_device(attach->dev);

	sysfs_entry->kobj.kset = dmabuf->sysfs_entry->attach_stats_kset;
	attach->sysfs_entry = sysfs_entry;
	kobject_init(&sysfs_entry->kobj, &dma_buf_attach_ktype);

	spin_lock_irqsave(&work_lock, flags);
	list_add(&attachment_work->list, &attachment_work_list);
	spin_unlock_irqrestore(&work_lock, flags);

	queue_work(system_wq, &attachment_work_struct);

	return 0;

free_attachment_work:
	kfree(attachment_work);

	return ret;
}

void dma_buf_stats_teardown(struct dma_buf *dmabuf)
{
	struct dma_buf_sysfs_entry *sysfs_entry;

	sysfs_entry = dmabuf->sysfs_entry;
	if (!sysfs_entry)
		return;

	kset_unregister(sysfs_entry->attach_stats_kset);
	kobject_del(&sysfs_entry->kobj);
	kobject_put(&sysfs_entry->kobj);
}

/*
 * Statistics files do not need to send uevents.
 */
static int dmabuf_sysfs_uevent_filter(struct kset *kset, struct kobject *kobj)
{
	return 0;
}

static const struct kset_uevent_ops dmabuf_sysfs_no_uevent_ops = {
	.filter = dmabuf_sysfs_uevent_filter,
};

static struct kset *dma_buf_stats_kset;
static struct kset *dma_buf_per_buffer_stats_kset;
int dma_buf_init_sysfs_statistics(void)
{
	dma_buf_stats_kset = kset_create_and_add("dmabuf",
						 &dmabuf_sysfs_no_uevent_ops,
						 kernel_kobj);
	if (!dma_buf_stats_kset)
		return -ENOMEM;

	dma_buf_per_buffer_stats_kset = kset_create_and_add("buffers",
							    &dmabuf_sysfs_no_uevent_ops,
							    &dma_buf_stats_kset->kobj);
	if (!dma_buf_per_buffer_stats_kset) {
		kset_unregister(dma_buf_stats_kset);
		return -ENOMEM;
	}

	INIT_WORK(&attachment_work_struct, process_attachment_workqueue);

	return 0;
}

void dma_buf_uninit_sysfs_statistics(void)
{
	kset_unregister(dma_buf_per_buffer_stats_kset);
	kset_unregister(dma_buf_stats_kset);
}

int dma_buf_stats_setup(struct dma_buf *dmabuf)
{
	struct dma_buf_sysfs_entry *sysfs_entry;
	int ret;
	struct kset *attach_stats_kset;

	if (!dmabuf || !dmabuf->file)
		return -EINVAL;

	if (!dmabuf->exp_name) {
		pr_err("exporter name must not be empty if stats needed\n");
		return -EINVAL;
	}

	sysfs_entry = kzalloc(sizeof(struct dma_buf_sysfs_entry), GFP_KERNEL);
	if (!sysfs_entry)
		return -ENOMEM;

	sysfs_entry->kobj.kset = dma_buf_per_buffer_stats_kset;
	sysfs_entry->dmabuf = dmabuf;

	dmabuf->sysfs_entry = sysfs_entry;

	/* create the directory for buffer stats */
	ret = kobject_init_and_add(&sysfs_entry->kobj, &dma_buf_ktype, NULL,
				   "%lu", file_inode(dmabuf->file)->i_ino);
	if (ret)
		goto err_sysfs_dmabuf;

	/* create the directory for attachment stats */
	attach_stats_kset = kset_create_and_add("attachments",
						&dmabuf_sysfs_no_uevent_ops,
						&sysfs_entry->kobj);
	if (!attach_stats_kset) {
		ret = -ENOMEM;
		goto err_sysfs_attach;
	}

	sysfs_entry->attach_stats_kset = attach_stats_kset;

	return 0;

err_sysfs_attach:
	kobject_del(&sysfs_entry->kobj);
err_sysfs_dmabuf:
	kobject_put(&sysfs_entry->kobj);
	dmabuf->sysfs_entry = NULL;
	return ret;
}
