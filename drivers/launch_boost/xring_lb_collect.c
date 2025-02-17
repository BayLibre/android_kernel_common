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

#include <linux/rculist.h>
#include <linux/workqueue.h>
#include <linux/ktime.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/rbtree.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/rwlock.h>
#include <linux/atomic.h>

#define CREATE_TRACE_POINTS
#include "xring_lb_trace.h"

#include "xring_lb_drv.h"
#include "xring_lb_def.h"
#include "xring_lb_collect.h"

rwlock_t user0_list_rw_lock;
LIST_HEAD(g_record_list_user0);
struct mutex g_lb_mutex;
atomic_t g_file_count;

static struct lb_collector *g_collector;
static struct app_record *g_active_record;
static LIST_HEAD(g_pid_candidates);
static rwlock_t g_pid_candidates_lock;

static bool xring_lb_skip_readfile(void)
{
	struct lb_pid_info *info;
	bool ret = true;

	read_lock(&g_pid_candidates_lock);
	if (list_empty(&g_pid_candidates))
		goto out;

	list_for_each_entry(info, &g_pid_candidates, list) {
		if (current->pid == info->pid) {
			ret = false;
			goto out;
		}
	}
out:
	read_unlock(&g_pid_candidates_lock);
	return ret;
}

static void xring_lb_cachepage_info_init(struct file *file, unsigned int off,
				ssize_t count, struct cachepage_info *info)
{
	info->filp = file;
	info->offset = off;
	info->count = count;
}

static void xring_lb_pagecache_collect(struct file *file, unsigned int pgoff,
							unsigned int count)
{
	struct cachepage_info info;
	int ret;

	BUG_ON(file_count(file) == 0);
	get_file(file);

	xring_lb_cachepage_info_init(file, pgoff, count, &info);
	XRING_LB_DBG("kfifo in file:0x%llx, offset:%d, size:%d\n",
		(unsigned long long)info.filp, info.offset, info.count);
	spin_lock(&g_collector->lock);
	ret = kfifo_in(&g_collector->fifo, &info, sizeof(info));
	spin_unlock(&g_collector->lock);
	if (ret == 0) {
		XRING_LB_INFO("kfifo in failed: %d\n", ret);
		fput(file);
		return;
	}

	atomic_set(&g_collector->collect_flag, F_NEED_COLLECT);
	/* just set wait condition if g_collector is not in waitqueue */
	if (waitqueue_active(&g_collector->wait))
		wake_up_all(&g_collector->wait);
}

void xring_lb_page_collect_on_readfile(
		void *unused, struct file *file, loff_t pos, size_t size)
{
	if (xring_lb_skip_readfile() || (size == 0))
		return;

	xring_lb_pagecache_collect(file, pos, size);
}

void xring_lb_page_collect_on_pagefault(void *unused, struct file *file,
					pgoff_t first_pgoff,
					pgoff_t last_pgoff,
					vm_fault_t ret)
{
	if (xring_lb_skip_readfile() || (last_pgoff == first_pgoff))
		return;

	xring_lb_pagecache_collect(file, first_pgoff << PAGE_SHIFT,
					(last_pgoff - first_pgoff + 1) << PAGE_SHIFT);
}

static void xring_lb_add_record_to_list(void)
{
	struct app_record *entry;

	read_lock(&user0_list_rw_lock);
	list_for_each_entry(entry, &g_record_list_user0, app_list)
		if ((entry->owner.uid == g_active_record->owner.uid) &&
			!strncmp(entry->owner.name, g_active_record->owner.name,
				PACKAGENAME_MAX_LEN - 1))
			goto unlock;
	read_unlock(&user0_list_rw_lock);

	write_lock(&user0_list_rw_lock);
	list_add(&g_active_record->app_list, &g_record_list_user0);
	write_unlock(&user0_list_rw_lock);

	return;
unlock:
	read_unlock(&user0_list_rw_lock);
}

struct app_record *xring_lb_get_record_from_list(const struct lb_owner *owner)
{
	struct app_record *record = NULL;
	struct list_head *pos = NULL;

	read_lock(&user0_list_rw_lock);
	list_for_each(pos, &g_record_list_user0) {
		record = container_of(pos, struct app_record, app_list);
		if ((record->owner.uid == owner->uid) &&
			!strncmp(record->owner.name, owner->name,
					PACKAGENAME_MAX_LEN - 1)) {
			read_unlock(&user0_list_rw_lock);
			return record;
		}
	}

	read_unlock(&user0_list_rw_lock);
	return NULL;
}

int xring_lb_dealwith_stop_cmd(unsigned long arg)
{
	const void __user *argp = (void __user *)(uintptr_t)arg;
	struct lb_owner owner;
	struct app_record *record;

	if (g_active_record == NULL) {
		XRING_LB_INFO("collect is not in progress\n");
		return 0;
	}

	if (copy_from_user(&owner, argp, sizeof(struct lb_owner))) {
		XRING_LB_ERR("copy from user fail");
		return -1;
	}
	XRING_LB_INFO("uid: %d, pid: %d, packagename: %s",
				owner.uid, owner.pid, owner.name);

	record = xring_lb_get_record_from_list(&owner);
	if (!record || record != g_active_record) {
		XRING_LB_INFO("record not match, skip this stop cmd\n");
		return 0;
	}

	schedule_delayed_work(&g_collector->record_work, 0);
	return 0;
}

int xring_lb_dealwith_collect_cmd(unsigned long arg)
{
	const void __user *argp = (void __user *)(uintptr_t)arg;
	struct lb_owner owner;
	struct lb_pid_info *pid_info;
	struct app_record *record;

	if (copy_from_user(&owner, argp, sizeof(struct lb_owner))) {
		XRING_LB_ERR("copy from user fail");
		return -1;
	}
	XRING_LB_INFO("uid: %d, pid: %d, packagename: %s, pid name: %s",
			owner.uid, owner.pid, owner.name,
			pid_task(find_get_pid(owner.pid), PIDTYPE_PID)->comm);

	record = xring_lb_get_record_from_list(&owner);
	if (!record || record != g_active_record) {
		if (!mutex_trylock(&g_lb_mutex)) {
			XRING_LB_INFO("collect in progress, skip\n");
			return 0;
		}
		if (!record) {
			XRING_LB_INFO("can not find record from list, alloc new one");
			record = kzalloc(sizeof(struct app_record), GFP_KERNEL);
			if (!record) {
				XRING_LB_ERR("alloc app_record fail");
				mutex_unlock(&g_lb_mutex);
				return -1;
			}
			record->owner = owner;
			INIT_LIST_HEAD(&record->file_list);
		}
		g_active_record = record;
		xring_lb_add_record_to_list();

		schedule_delayed_work(&g_collector->record_work, msecs_to_jiffies(800));
	}

	pid_info = kzalloc(sizeof(struct lb_pid_info), GFP_KERNEL);
	if (!pid_info) {
		XRING_LB_ERR("Failed to allocate memory for new pid info");
		return -1;
	}
	pid_info->pid = owner.pid;

	write_lock(&g_pid_candidates_lock);
	list_add(&pid_info->list, &g_pid_candidates);
	write_unlock(&g_pid_candidates_lock);

	return 0;
}

static int get_file_with_same_inode(struct file_info **file_info, char *file_path)
{
	struct list_head *pos = NULL;
	struct app_record *record = g_active_record;
	int idx = 0;

	if (list_empty(&record->file_list))
		return -1;

	list_for_each(pos, &record->file_list) {
		struct file_info *info =
			container_of(pos, struct file_info, list);

		if (strlen(file_path) == info->path_len &&
			!strncmp(file_path, info->path, info->path_len)) {
			*file_info = info;
			return idx;
		}

		idx++;
	}

	return -1;
}

static struct file_info *xring_lb_alloc_fileinfo(void)
{
	struct file_info *fileinfo;

	fileinfo = kzalloc(sizeof(struct file_info), GFP_KERNEL);
	if (!fileinfo)
		return NULL;

	return fileinfo;
}

static struct file_info *xring_lb_new_file_tree(struct file *file,
		char *filepath, int path_len)
{
	struct file_info *info = NULL;

	info = xring_lb_alloc_fileinfo();
	if (!info) {
		XRING_LB_ERR("alloc file info failed");
		return NULL;
	}

	info->rbroot = RB_ROOT;
	info->interval_count = 0;
	info->i_ino = file_inode(file)->i_ino;
	info->i_mtime = file_inode(file)->i_mtime;
	mutex_init(&info->rbtree_lock);

	if ((filepath != NULL) && (path_len != 0)) {
		info->path = kzalloc(path_len + 1, GFP_KERNEL);
		if (!info->path) {
			XRING_LB_ERR("info->path alloc fail\n");
			kfree(info);
			return NULL;
		}
		strncpy(info->path, filepath, path_len);
		info->path[path_len] = '\0';
		info->path_len = path_len;
		XRING_LB_DBG("add new file: %s, path len: %d\n", info->path, info->path_len);
	}

	return info;
}

static int xring_lb_get_fileinfo(struct file *file, struct file_info **file_info, int *file_idx)
{
	char *filepath;

	filepath = file_path(file, g_collector->file_path, PATH_MAX);
	if (IS_ERR(filepath)) {
		XRING_LB_ERR("find filepath error\n");
		return -ENOENT;
	}

	*file_idx = get_file_with_same_inode(file_info, filepath);
	if (*file_idx >= 0)
		return 0;

	/* this is new file */
	*file_info = xring_lb_new_file_tree(file, filepath, strlen(filepath));
	if (*file_info == NULL)
		return -EPERM;

	return 0;
}

/*
 * @root: rb_root of the file
 * @new_interval: new file_interval to be inserted into file
 * return false: no overlapped or adjoined with any ranges
 * return true: merge happens
 */
void xring_lb_insert_interval(struct file_info *file_info, struct file_interval *new_interval)
{
	struct rb_root *root = &file_info->rbroot;
	struct rb_node **new = &(root->rb_node);
	struct rb_node *parent = NULL;
	struct file_interval *merged_interval = new_interval;

	while (*new) {
		struct file_interval *this = container_of(*new, struct file_interval, node);

		parent = *new;
		if (merged_interval->end < this->start)
			new = &((*new)->rb_left);
		else if (merged_interval->start > this->end)
			new = &((*new)->rb_right);
		else {
			if (merged_interval->start < this->start)
				this->start = merged_interval->start;
			if (merged_interval->end > this->end)
				this->end = merged_interval->end;
			kfree(merged_interval);
			merged_interval = this;

			rb_erase(&this->node, root);
			new = &(root->rb_node);
			parent = NULL;
		}
	}

	rb_link_node(&merged_interval->node, parent, new);
	rb_insert_color(&merged_interval->node, root);
}

static int xring_lb_process_cachepage_info(struct cachepage_info *info)
{
	struct file_info *file_info = NULL;
	struct file_interval *interval = NULL;
	int ret;
	int file_idx = -1;

	ret = xring_lb_get_fileinfo(info->filp, &file_info, &file_idx);
	if (ret < 0) {
		XRING_LB_ERR("get fileinfo from list err");
		fput(info->filp);
		return -1;
	}

	if (file_idx < 0) {
		file_info->record = g_active_record;
		list_add_tail(&file_info->list, &g_active_record->file_list);
	}

	fput(info->filp);

	interval = kmalloc(sizeof(struct file_interval), GFP_KERNEL);
	if (!interval) {
		XRING_LB_ERR("alloc struct file_interval fail");
		kfree(file_info);
		return -ENOMEM;
	}

	interval->start = info->offset;
	interval->end = interval->start + info->count;
	trace_xring_lb_collect(file_info, interval->start, interval->end);

	/* now we have a file_info */
	mutex_lock(&file_info->rbtree_lock);
	xring_lb_insert_interval(file_info, interval);
	mutex_unlock(&file_info->rbtree_lock);

	return 0;
}

static void xring_lb_collector_do_record(void)
{
	struct lb_pid_info *entry;
	struct lb_pid_info *next;

	if (g_active_record == NULL) {
		XRING_LB_INFO("record is complete\n");
		return;
	}

	/* clear g_pid_list, be ready for next app */
	write_lock(&g_pid_candidates_lock);
	list_for_each_entry_safe(entry, next, &g_pid_candidates, list) {
		list_del(&entry->list);
		kfree(entry);
	}
	write_unlock(&g_pid_candidates_lock);

	/* collect is done, we don't need to keep current app info */
	g_active_record = NULL;
	mutex_unlock(&g_lb_mutex);
}

static void xring_lb_do_record(struct work_struct *work)
{
	atomic_set(&g_collector->record_flag, F_NEED_RECORD);
	wake_up_all(&g_collector->wait);
}

static void xring_lb_collector_do_collect(void)
{
	struct cachepage_info info;
	int ret;

	if (g_active_record == NULL) {
		XRING_LB_INFO("record is complete\n");
		return;
	}

	while (!kfifo_is_empty(&g_collector->fifo)) {
		ret = kfifo_out(&g_collector->fifo, &info, sizeof(info));
		if (ret == 0) {
			XRING_LB_ERR("kfifo is empty\n");
			return;
		}
		XRING_LB_DBG("kfifo out file:0x%llx, offset:%d, count:%d\n",
			(unsigned long long)info.filp, info.offset, info.count);

		xring_lb_process_cachepage_info(&info);
	}
}

static void xring_lb_collector_do_work(struct lb_collector *collector)
{
	if (atomic_read(&g_collector->collect_flag) == F_NEED_COLLECT) {
		atomic_set(&g_collector->collect_flag, F_NONE);
		xring_lb_collector_do_collect();
	}

	if (atomic_read(&g_collector->record_flag) == F_NEED_RECORD) {
		atomic_set(&g_collector->record_flag, F_NONE);
		xring_lb_collector_do_record();
	}
}

static int xring_lb_collect_thread(void *data)
{
	struct lb_collector *collector = data;

	if (!data) {
		XRING_LB_ERR("data is null");
		return -EINVAL;
	}

	while (!kthread_should_stop()) {
		if (wait_event_interruptible(collector->wait,
			(atomic_read(&collector->collect_flag) == F_NEED_COLLECT ||
			atomic_read(&collector->record_flag) == F_NEED_RECORD)))
			return -1;

		xring_lb_collector_do_work(collector);
	}

	return 0;
}

int xring_lb_init_collector(void)
{
	int ret = 0;

	if (COLLECTOR_SIZE % sizeof(struct cachepage_info)) {
		XRING_LB_ERR("size of COLLECTOR is not aligned with %lu",
				sizeof(struct cachepage_info));
		ret = -EINVAL;
		goto out;
	}

	g_collector = kzalloc(sizeof(struct lb_collector), GFP_KERNEL);
	if (!g_collector) {
		XRING_LB_ERR("alloc g_collector fail");
		ret = -EINVAL;
		goto out;
	}

	ret = kfifo_alloc(&g_collector->fifo, COLLECTOR_SIZE, GFP_KERNEL);
	if (ret) {
		XRING_LB_ERR("alloc kfifo failed\n");
		goto alloc_g_collector_fail;
	}

	rwlock_init(&user0_list_rw_lock);
	rwlock_init(&g_pid_candidates_lock);
	spin_lock_init(&g_collector->lock);
	init_waitqueue_head(&g_collector->wait);
	INIT_DELAYED_WORK(&g_collector->record_work, xring_lb_do_record);

	mutex_init(&g_lb_mutex);
	atomic_set(&g_collector->collect_flag, 0);
	atomic_set(&g_collector->record_flag, 0);

	g_collector->collect_thread = kthread_run(
		xring_lb_collect_thread,
		g_collector, "lb:collector");
	if (IS_ERR(g_collector->collect_thread)) {
		ret = PTR_ERR(g_collector->collect_thread);
		XRING_LB_ERR("create collect_thread fail, ret: %d", ret);
		goto create_collect_thread_fail;
	}

	g_lb_data.collector = g_collector;
	return 0;

create_collect_thread_fail:
	kfifo_free(&g_collector->fifo);
alloc_g_collector_fail:
	kfree(g_collector);
	g_collector = NULL;
out:
	return ret;
}
