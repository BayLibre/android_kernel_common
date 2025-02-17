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

#ifndef _XRING_LB_PREREAD_H_
#define _XRING_LB_PREREAD_H_

struct lb_preread_thread_data {
	struct task_struct *preread_thread;
	struct list_head staging_list;
	spinlock_t lock;
	atomic_t do_preread;
};

struct lb_preread {
	wait_queue_head_t wait;
	struct lb_preread_thread_data data;
};


int xring_lb_init_preread(void);
int xring_lb_dealwith_preread_cmd(unsigned long arg);

#endif /* _XRING_LB_PREREAD_H */
