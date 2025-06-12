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
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/namei.h>
#include <linux/dcache.h>
#include <linux/dirent.h>
#include <linux/err.h>
#include <linux/file.h>
#include <linux/list.h>

#include "xring_lb_collect.h"
#include "xring_lb_clear.h"
#include "xring_lb_def.h"
#include "xring_lb_drv.h"
#include "xring_lb_flush.h"

void xring_lb_free_one_fileinfo(struct file_info *file_entry)
{
	struct file_interval *interval;
	struct rb_node *node;

	list_del(&file_entry->list);

	while ((node = rb_first(&file_entry->rbroot))) {
		interval = rb_entry(node, struct file_interval, node);
		rb_erase(node, &file_entry->rbroot);
		kfree(interval);
	}
	kfree(file_entry->path);
	kfree(file_entry);
}

void xring_lb_free_one_record(struct app_record *record)
{
	struct file_info *file_entry, *tmp;

	write_lock(&user0_list_rw_lock);
	list_del(&record->app_list);
	write_unlock(&user0_list_rw_lock);

	list_for_each_entry_safe(file_entry, tmp, &record->file_list, list)
		xring_lb_free_one_fileinfo(file_entry);

	kfree(record);
}

int xring_lb_unlink_one_record(const char *record_name)
{
	struct file *filp;
	struct dentry *parent_dentry;
	struct dentry *dentry;
	struct inode *pinode;

	filp = filp_open(s_path_user0, O_RDONLY | O_DIRECTORY, 0);
	if (IS_ERR(filp)) {
		XRING_LB_ERR("Failed to open file: %s\n", s_path_user0);
		return -1;
	}

	parent_dentry = filp->f_path.dentry;
	pinode = d_inode(parent_dentry);

	inode_lock_nested(pinode, I_MUTEX_PARENT);
	dentry = lookup_one_len(record_name, parent_dentry, strlen(record_name));
	if (IS_ERR(dentry)) {
		XRING_LB_ERR("Failed to lookup file: %s\n", record_name);
		inode_unlock(pinode);
		filp_close(filp, NULL);
		return -1;
	}

	vfs_unlink(&nop_mnt_idmap, pinode, dentry, NULL);

	inode_unlock(pinode);
	dput(dentry);
	filp_close(filp, NULL);
	return 0;
}

int xring_lb_dealwith_clear_cmd(unsigned long arg)
{
	const void __user *argp = (void __user *)(uintptr_t)arg;
	struct lb_owner owner;
	struct app_record *record;
	int error = 0;

	if (!mutex_trylock(&g_lb_mutex)) {
		XRING_LB_INFO("doing collect, skip clear\n");
		return -1;
	}

	error = copy_from_user(&owner, argp, sizeof(struct lb_owner));
	if (error) {
		XRING_LB_ERR("copy from user fail\n");
		goto unlock;
	}

	XRING_LB_INFO("uid: %d, pid: %d, packagename: %s",
			owner.uid, owner.pid, owner.name);

	record = xring_lb_get_record_from_list(&owner);
	if (!record) {
		XRING_LB_ERR("get record from list failed\n");
		error = -1;
		goto unlock;
	}

	if (atomic_read(&g_file_count)) {
		XRING_LB_ERR("record in preread, skip clear\n");
		error = -1;
		goto unlock;
	}

	error = xring_lb_unlink_one_record(record->owner.name);
	if (error)
		XRING_LB_ERR("unlink record fail, do free\n");

	error = 0;
	xring_lb_free_one_record(record);

unlock:
	mutex_unlock(&g_lb_mutex);

	return error;
}
