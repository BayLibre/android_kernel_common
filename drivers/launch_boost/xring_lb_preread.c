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

#include <linux/fs.h>
#include <linux/rbtree.h>
#include <linux/list.h>
#include <linux/kthread.h>
#include <linux/fadvise.h>
#include <linux/atomic.h>
#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/ioprio.h>

#include "xring_lb_trace.h"

#include "xring_lb_drv.h"
#include "xring_lb_def.h"
#include "xring_lb_collect.h"
#include "xring_lb_preread.h"
#include "xring_lb_clear.h"

struct sched_param {
	int sched_priority;
};

static struct lb_preread *s_preread;

static int read_intervals_from_file(struct file_info *finfo)
{
	struct rb_node *node;
	struct file_interval *interval;
	struct file *filp;
	int ret = 0;

	filp = finfo->filp;
	for (node = rb_first(&finfo->rbroot); node; node = rb_next(node)) {
		interval = rb_entry(node, struct file_interval, node);
		ret = vfs_fadvise(filp, interval->start,
				interval->end - interval->start,
				POSIX_FADV_WILLNEED);
		if (ret) {
			XRING_LB_ERR("vfs_fadvise error, ret: %d\n", ret);
			goto out;
		}

		XRING_LB_DBG("T: %p, preread file: %s, start: %d, end: %d\n",
			current, finfo->path, interval->start, interval->end);
		trace_xring_lb_preread(finfo, interval->start, interval->end);
	}

	list_del_init(&finfo->preread_list);
out:
	filp_close(filp, NULL);
	return ret;
}

static void xring_lb_wake_up_preread(struct app_record *record)
{
	struct file_info *info, *tmp;
	struct lb_preread_thread_data *data;
	struct file *filp;
	struct inode *inode;

	if (atomic_read(&g_file_count)) {
		XRING_LB_ERR("preread in progress, skip preread\n");
		return;
	}

	data = &s_preread->data;
	atomic_set(&g_file_count, 0);
	list_for_each_entry_safe(info, tmp, &record->file_list, list) {
		filp = filp_open(info->path, O_RDONLY, 0);
		if (IS_ERR(filp)) {
			XRING_LB_INFO("open file path: %s failed, next", info->path);
			xring_lb_free_one_fileinfo(info);
			continue;
		}

		inode = filp->f_inode;
		if (inode->i_ino != info->i_ino || inode->i_mtime.tv_sec !=
						info->i_mtime.tv_sec) {
			XRING_LB_INFO("inode has been modified, delete this file info\n");
			filp_close(filp, NULL);
			xring_lb_free_one_fileinfo(info);
			continue;
		}

		info->filp = filp;
		spin_lock(&data->lock);
		list_add_tail(&info->preread_list, &data->staging_list);
		spin_unlock(&data->lock);

		atomic_inc(&g_file_count);
		atomic_set(&data->do_preread, 1);
		/* just set wait condition if s_preread is not in waitqueue */
		if (waitqueue_active(&s_preread->wait))
			wake_up_all(&s_preread->wait);
	}
}

int xring_lb_dealwith_preread_cmd(unsigned long arg)
{
	const void __user *argp = (void __user *)(uintptr_t)arg;
	struct lb_owner owner;
	struct app_record *record;
	int ret = 0;

	if (!mutex_trylock(&g_lb_mutex)) {
		XRING_LB_INFO("record in progress, skip preread\n");
		return ret;
	}

	ret = copy_from_user(&owner, argp, sizeof(struct lb_owner));
	if (ret) {
		XRING_LB_ERR("copy from user fail, ret:%d\n", ret);
		goto out;
	}

	XRING_LB_INFO("uid: %d, pid: %d, packagename: %s",
			owner.uid, owner.pid, owner.name);

	record = xring_lb_get_record_from_list(&owner);
	if (!record) {
		XRING_LB_INFO("can not find from list");
		goto out;
	}

	xring_lb_wake_up_preread(record);

out:
	mutex_unlock(&g_lb_mutex);
	return ret;
}

static int xring_lb_do_preread(struct lb_preread_thread_data *data)
{
	struct file_info *info, *tmp;
	LIST_HEAD(list);
	int ret;

	spin_lock(&data->lock);
	if (!list_empty(&data->staging_list))
		list_splice_init(&data->staging_list, &list);
	spin_unlock(&data->lock);

	list_for_each_entry_safe(info, tmp, &list, preread_list) {
		XRING_LB_INFO("current pid: 0x%x, preread file: %s\n",
						current->pid, info->path);
		mutex_lock(&info->rbtree_lock);
		ret = read_intervals_from_file(info);
		if (ret)
			XRING_LB_ERR("read intervals from file failed\n");
		mutex_unlock(&info->rbtree_lock);
		atomic_dec(&g_file_count);
	}

	return 0;
}

int xring_lb_preread_thread(void *data)
{
	struct lb_preread_thread_data *thread_data = data;

	if (!thread_data) {
		XRING_LB_ERR("data is null\n");
		return -EINVAL;
	}

	while (!kthread_should_stop()) {
		if (wait_event_interruptible(s_preread->wait,
				atomic_read(&thread_data->do_preread)))
			return -1;

		atomic_set(&thread_data->do_preread, 0);
		xring_lb_do_preread(thread_data);
	}

	return 0;
}

int xring_lb_init_preread(void)
{
	int ret;
	struct lb_preread_thread_data *data;
	struct sched_param param = { .sched_priority = MAX_RT_PRIO - 1 };

	s_preread = kzalloc(sizeof(struct lb_preread), GFP_KERNEL);
	if (!s_preread) {
		XRING_LB_ERR("alloc s_preread fail");
		return -1;
	}

	init_waitqueue_head(&s_preread->wait);

	data = &s_preread->data;
	atomic_set(&data->do_preread, 0);
	data->preread_thread = kthread_run(
		xring_lb_preread_thread, data, "lb:preread");
	if (IS_ERR(data->preread_thread)) {
		ret = PTR_ERR(data->preread_thread);
		XRING_LB_ERR("create preread_thread fail, ret: %d", ret);
		kfree(s_preread);
		return ret;
	}
	set_task_ioprio(data->preread_thread, IOPRIO_PRIO_VALUE(IOPRIO_CLASS_BE, 0));
	sched_setscheduler_nocheck(data->preread_thread, SCHED_FIFO, &param);

	INIT_LIST_HEAD(&data->staging_list);
	spin_lock_init(&data->lock);
	g_lb_data.preread = s_preread;

	return 0;
}
