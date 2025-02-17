/* SPDX-License-Identifier: GPL-2.0-only
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

#ifndef _XRING_LB_COLLECT_H_
#define _XRING_LB_COLLECT_H_

#include <linux/fs.h>
#include <linux/ctype.h>
#include <linux/wait.h>
#include <linux/kfifo.h>

#include "xring_lb_drv.h"

#define COLLECTOR_SIZE	(PAGE_SIZE * 256)

#define BUFFER_FETCH_COUNT (64)

enum {
	F_NONE = 0,
	F_NEED_COLLECT,
	F_NEED_RECORD,
	F_MAX
};

struct cachepage_info {
	struct file *filp;
	unsigned int offset;
	unsigned int count;
};

struct file_info {
	struct app_record *record;
	struct file *filp;
	struct list_head list;
	struct list_head preread_list;
	struct rb_root rbroot;
	struct mutex rbtree_lock;
	union {
		struct file_info_disk {
			unsigned long i_ino;
			struct timespec64 i_mtime;
			unsigned int interval_count;
			unsigned int path_len;
			char *path;
		} file_disk;
		struct {
			unsigned long i_ino;
			struct timespec64 i_mtime;
			unsigned int interval_count;
			unsigned int path_len;
			char *path;
		};
	};
};

struct file_interval {
	struct rb_node node;
	union {
		struct file_interval_disk {
			unsigned int start;
			unsigned int end;
		} interval_disk;
		struct {
			unsigned int start;
			unsigned int end;
		};
	};
};

struct app_record {
	struct list_head app_list;
	struct list_head file_list;
	struct list_head flush_list;
	unsigned int file_size;
	unsigned int mm_size;
	union {
		struct app_record_disk {
			unsigned int file_cnt;
			struct lb_owner owner;
			unsigned int file_disk_size;
		} record_disk;
		struct {
			unsigned int file_cnt;
			struct lb_owner owner;
			unsigned int file_disk_size;
		};
	};
};

struct lb_collector {
	spinlock_t lock;
	struct task_struct *collect_thread;
	wait_queue_head_t wait;
	atomic_t collect_flag;
	atomic_t record_flag;
	struct workqueue_start *record_wq;
	struct delayed_work record_work;
	struct kfifo fifo;
	char file_path[PATH_MAX];
};

void xring_lb_page_collect_on_readfile(void *unused,
				struct file *file, loff_t pos, size_t size);
void xring_lb_page_collect_on_pagefault(void *unused, struct file *file,
					pgoff_t first_pgoff,
					pgoff_t last_pgoff,
					vm_fault_t ret);
int xring_lb_dealwith_collect_cmd(unsigned long arg);
int xring_lb_dealwith_stop_cmd(unsigned long arg);
int xring_lb_collect_pids(unsigned long arg);
struct app_record *xring_lb_get_record_from_list(const struct lb_owner *owner);
int xring_lb_init_collector(void);
void xring_lb_insert_interval(struct file_info *file_info,
				struct file_interval *new_interval);
extern struct list_head g_record_list_user0;
extern rwlock_t user0_list_rw_lock;
extern struct mutex g_lb_mutex;
extern atomic_t g_file_count;

#endif /* _XRING_LB_COLLECT_H_ */
