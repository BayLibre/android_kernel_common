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
#include <linux/vmalloc.h>
#include <linux/file.h>
#include <linux/crc32.h>
#include <linux/security.h>
#include <linux/workqueue.h>
#include <linux/list.h>

#include "xring_lb_collect.h"
#include "xring_lb_clear.h"
#include "xring_lb_flush.h"
#include "xring_lb_drv.h"
#include "xring_lb_def.h"

char s_path_user0[MAX_PATH_LEN] = "/data/misc/lb_user0/";

static unsigned int xring_lb_get_single_file_size(struct file_info *file_info)
{
	struct rb_node *node;

	file_info->interval_count = 0;
	for (node = rb_first(&file_info->rbroot); node;
			node = rb_next(node))
		file_info->interval_count++;

	return sizeof(struct file_info_disk) +
		file_info->path_len +
		file_info->interval_count * sizeof(struct file_interval_disk);
}

static int xring_lb_flush_one_record(struct app_record *record)
{
	struct file *filp;
	char name_buf[PACKAGENAME_MAX_LEN + MAX_PATH_LEN];
	char *path;
	struct lb_flush_header header;
	struct file_info *file_entry;
	struct file_interval *interval;
	struct rb_node *node;
	char *buf, *temp;
	ssize_t cnt;
	loff_t pos;
	int error = 0;

	path = strcat(name_buf, s_path_user0);
	path = strcat(path, record->owner.name);

	filp = filp_open(path, O_CREAT | O_RDWR, 0666);
	if (IS_ERR(filp)) {
		XRING_LB_ERR("Failed to open file: %s, err: %ld\n", path, PTR_ERR(filp));
		return -1;
	}

	header.len = sizeof(struct app_record_disk);
	record->file_cnt = 0;
	list_for_each_entry(file_entry, &record->file_list, list) {
		header.len += xring_lb_get_single_file_size(file_entry);
		record->file_cnt++;
	}
	record->file_disk_size = sizeof(struct file_info_disk);

	temp = vmalloc(header.len);
	if (!temp) {
		XRING_LB_ERR("vmalloc buf failed\n");
		error = -1;
		goto close_file;
	}

	buf = temp;
	memcpy(buf, &record->record_disk, sizeof(struct app_record_disk));
	buf += sizeof(struct app_record_disk);

	list_for_each_entry(file_entry, &record->file_list, list) {
		memcpy(buf, &file_entry->file_disk, sizeof(struct file_info_disk));
		buf += sizeof(struct file_info_disk);
		memcpy(buf, file_entry->path, file_entry->path_len);
		buf += file_entry->path_len;

		for (node = rb_first(&file_entry->rbroot); node;
					node = rb_next(node)) {
			interval = container_of(node, struct file_interval, node);
			memcpy(buf, &interval->interval_disk, sizeof(struct file_interval_disk));
			buf += sizeof(struct file_interval_disk);
		}
	}
	header.crc = crc32(0, temp, header.len);
	header.record_disk_size = sizeof(struct app_record_disk);

	error = vfs_truncate(&filp->f_path, header.len + sizeof(struct lb_flush_header));
	if (error) {
		XRING_LB_ERR("truncate file failed, error:%d\n", error);
		goto free_buf;
	}

	pos = 0;
	cnt = kernel_write(filp, &header, sizeof(struct lb_flush_header), &pos);
	if (cnt < 0) {
		XRING_LB_ERR("kernel_write failed, cnt:%zd\n", cnt);
		error = -1;
		goto free_buf;
	}

	cnt = kernel_write(filp, temp, header.len, &pos);
	if (cnt < 0) {
		XRING_LB_ERR("kernel_write failed, cnt:%zd\n", cnt);
		error = -1;
		goto free_buf;
	}

	error = vfs_fsync(filp, 0);
	if (error)
		XRING_LB_ERR("vfs_fsync failed, error:%d\n", error);

free_buf:
	vfree(temp);
close_file:
	filp_close(filp, NULL);

	return error;
}

static bool read_file(struct dir_context *ctx, const char *name, int namlen,
		       loff_t offset, u64 ino, unsigned int d_type)
{
	struct file *filp;
	struct lb_flush_header header;
	char name_buf[PACKAGENAME_MAX_LEN + MAX_PATH_LEN];
	char *path;
	ssize_t cnt;
	loff_t pos;
	char *buf, *temp;
	unsigned int crc;
	struct app_record *record;
	struct file_info *file_entry;
	struct file_interval *interval;
	int file_size;
	int i, j;

	if (!strncmp(name, ".", strlen(name)) || !strncmp(name, "..", strlen(name)))
		return true;

	path = strcat(name_buf, s_path_user0);
	path = strcat(path, name);

	filp = filp_open(path, O_RDONLY, 0);
	if (IS_ERR(filp)) {
		XRING_LB_ERR("cannot open path for %s\n", path);
		return true;
	}

	file_size = filp->f_inode->i_size;

	pos = 0;
	cnt = kernel_read(filp, &header, sizeof(struct lb_flush_header), &pos);
	if (cnt < 0 || header.len == 0) {
		XRING_LB_ERR("cannot read file %s\n", name);
		goto close_file;
	}

	if (header.len != (file_size - sizeof(struct lb_flush_header)) ||
		header.record_disk_size != sizeof(struct app_record_disk)) {
		XRING_LB_ERR("file size or record size error, file corruption\n");
		__fput_sync(filp);
		xring_lb_unlink_one_record(name);
		return true;
	}

	temp = vmalloc(header.len);
	if (!temp) {
		XRING_LB_ERR("alloc buf failed\n");
		goto close_file;
	}
	buf = temp;

	XRING_LB_DBG("read app record, size: %d\n", header.len);
	cnt = kernel_read(filp, buf, header.len, &pos);
	if (cnt < 0) {
		XRING_LB_ERR("cannot read file %s\n", name);
		goto free_buf;
	}

	crc = crc32(0, buf, header.len);
	if (crc != header.crc) {
		XRING_LB_ERR("crc check error, file corruption\n");
		__fput_sync(filp);
		xring_lb_unlink_one_record(name);
		vfree(temp);
		return true;
	}

	record = kzalloc(sizeof(struct app_record), GFP_KERNEL);
	if (!record) {
		XRING_LB_ERR("alloc app_record fail\n");
		goto free_buf;
	}

	memcpy(&record->record_disk, buf, sizeof(struct app_record_disk));
	buf += sizeof(struct app_record_disk);
	INIT_LIST_HEAD(&record->file_list);
	list_add(&record->app_list, &g_record_list_user0);

	if (record->file_disk_size != sizeof(struct file_info_disk)) {
		XRING_LB_ERR("file info disk size check error\n");
		goto free_record;
	}

	for (i = 0; i < record->file_cnt; i++) {
		file_entry = kzalloc(sizeof(struct file_info), GFP_KERNEL);
		if (!file_entry) {
			XRING_LB_ERR("alloc file info fail\n");
			goto free_record;
		}

		memcpy(&file_entry->file_disk, buf, sizeof(struct file_info_disk));
		buf += sizeof(struct file_info_disk);

		file_entry->rbroot = RB_ROOT;
		mutex_init(&file_entry->rbtree_lock);
		list_add_tail(&file_entry->list, &record->file_list);

		file_entry->path = kzalloc(file_entry->path_len + 1, GFP_KERNEL);
		if (!file_entry->path) {
			XRING_LB_ERR("alloc file path fail\n");
			goto free_record;
		}
		memcpy(file_entry->path, buf, file_entry->path_len);
		file_entry->path[file_entry->path_len] = '\0';
		buf += file_entry->path_len;

		for (j = 0; j < file_entry->interval_count; j++) {
			interval = kzalloc(sizeof(struct file_interval), GFP_KERNEL);
			if (!interval) {
				XRING_LB_ERR("alloc file interval fail\n");
				goto free_record;
			}
			memcpy(&interval->interval_disk, buf, sizeof(struct file_interval_disk));
			buf += sizeof(struct file_interval_disk);
			xring_lb_insert_interval(file_entry, interval);
		}
	}

	vfree(temp);
	filp_close(filp, NULL);
	return true;

free_record:
	xring_lb_free_one_record(record);
free_buf:
	vfree(temp);
close_file:
	filp_close(filp, NULL);

	return true;
}

static int xring_lb_read_all_records(const char *pathname)
{
	struct file *filp;
	struct dir_context ctx = {.actor = read_file};
	int error;

	filp = filp_open(pathname, O_RDONLY | O_DIRECTORY, 0);
	if (IS_ERR(filp)) {
		XRING_LB_ERR("cannot open path for %s\n", pathname);
		return -1;
	}

	error = iterate_dir(filp, &ctx);
	if (error)
		XRING_LB_ERR("iterate_dir failed\n");

	filp_close(filp, NULL);
	return error;
}

static int xring_lb_create_record_folder(const char *pathname, umode_t mode)
{
	struct dentry *dentry;
	struct path path;
	int error;

	error = kern_path(pathname, LOOKUP_FOLLOW, &path);
	if (!error) {
		XRING_LB_INFO("kernel path exist\n");
		return 0;
	}

	dentry = kern_path_create(AT_FDCWD, pathname, &path, LOOKUP_DIRECTORY);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);
	error = vfs_mkdir(mnt_idmap(path.mnt), path.dentry->d_inode,
				dentry, mode);
	if (error) {
		XRING_LB_ERR("mkdir failed for %s\n", pathname);
		return error;
	}

	done_path_create(&path, dentry);
	return error;
}

int xring_lb_dealwith_flush_cmd(unsigned long arg)
{
	struct app_record *entry, *temp;
	int error = 0;
	LIST_HEAD(list);

	if (!mutex_trylock(&g_lb_mutex))
		return 0;

	error = xring_lb_create_record_folder(s_path_user0, 0777);
	if (error) {
		XRING_LB_ERR("create user foled error: %s\n", s_path_user0);
		goto unlock;
	}

	read_lock(&user0_list_rw_lock);
	list_for_each_entry(entry, &g_record_list_user0, app_list)
		list_add(&entry->flush_list, &list);
	read_unlock(&user0_list_rw_lock);

	list_for_each_entry_safe(entry, temp, &list, flush_list) {
		error = xring_lb_flush_one_record(entry);
		if (error < 0) {
			XRING_LB_ERR("flush record failed, err: %d\n", error);
			goto unlock;
		}
		list_del_init(&entry->flush_list);
	}

unlock:
	mutex_unlock(&g_lb_mutex);
	return error;
}

int xring_lb_dealwith_recovery_cmd(unsigned long arg)
{
	int error;

	error = xring_lb_read_all_records(s_path_user0);
	if (error)
		XRING_LB_ERR("read records fail: %s\n", s_path_user0);

	return 0;
}
