// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024-2024 XRing Technologies Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/stat.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/device.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>

#include "xring_lb_dbg.h"
#include "xring_lb_collect.h"
#include "xring_lb_def.h"

int g_lb_msg_level = 1;

static unsigned int xring_lb_get_single_file_size(struct file_info *file_info)
{
	struct rb_node *node;
	unsigned int interval_count;

	interval_count = 0;
	for (node = rb_first(&file_info->rbroot); node;
			node = rb_next(node))
		interval_count++;

	return sizeof(struct file_info) +
		file_info->path_len +
		interval_count * sizeof(struct file_interval);
}

static unsigned int xring_lb_scan_rbtree(struct file_info *file_info)
{
	struct rb_node *node;
	struct file_interval *data;
	struct rb_root *root = &file_info->rbroot;
	unsigned int size = 0;

	for (node = rb_first(root); node; node = rb_next(node)) {
		data = container_of(node, struct file_interval, node);
		size += data->end - data->start;
	}

	return size;
}

static void xring_lb_scan_file_list(struct app_record *entry)
{
	struct file_info *file_entry = NULL;

	entry->file_size = 0;
	entry->mm_size   = sizeof(struct app_record);

	list_for_each_entry(file_entry, &entry->file_list, list) {
		entry->file_size += xring_lb_scan_rbtree(file_entry);
		entry->mm_size   += xring_lb_get_single_file_size(file_entry);
	}
}

static int lb_collect_file_show(struct seq_file *m, void *v)
{
	struct app_record *entry;
	unsigned int app_index = 0;

	if (!mutex_trylock(&g_lb_mutex))
		return 0;

	if (list_empty(&g_record_list_user0)) {
		seq_puts(m, "g_record_list_user0 is empty\n");
		mutex_unlock(&g_lb_mutex);
		return 0;
	}

	list_for_each_entry(entry, &g_record_list_user0, app_list) {
		xring_lb_scan_file_list(entry);

		seq_printf(m, "[%d] app:%s, uid:%d, file size: %d, mm size: %d\n",
				app_index++,
				entry->owner.name,
				entry->owner.uid,
				entry->file_size,
				entry->mm_size);
	}

	mutex_unlock(&g_lb_mutex);
	return 0;
}

static int lb_collect_file_open(struct inode *inode, struct file *file)
{
	return single_open(file, lb_collect_file_show, NULL);
}

static const struct proc_ops lb_collect_file_fops = {
	.proc_open	= lb_collect_file_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

static int xring_lb_create_proc(void)
{
	struct proc_dir_entry *lb_dir, *lb_collect;

	lb_dir = proc_mkdir("launch_boost", NULL);
	if (!lb_dir) {
		XRING_LB_ERR("proc_mkdir failed\n");
		return -ENOMEM;
	}

	lb_collect = proc_create("collect", 0, lb_dir, &lb_collect_file_fops);
	if (!lb_collect) {
		remove_proc_entry("launch_boost", NULL);
		XRING_LB_ERR("proc_create lb_collect failed\n");
		return -ENOMEM;
	}

	return 0;
}

int xring_lb_dbg_init(void)
{
	int error;

	error = xring_lb_create_proc();
	if (error)
		XRING_LB_ERR("xring_lb_create_proc failed\n");

	return error;
}
