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

#ifndef _XRING_LB_DRV_H_
#define _XRING_LB_DRV_H_

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/sysfs.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#define LB_DEV_NAME "launch_boost"

#define LB_MAGIC 'L'

#define LB_COLLECT   _IOWR(LB_MAGIC, 0x1, struct lb_owner)
#define LB_PREREAD   _IOWR(LB_MAGIC, 0x2, struct lb_owner)
#define LB_CLEAR     _IOWR(LB_MAGIC, 0x3, struct lb_owner)
#define LB_FLUSH     _IOWR(LB_MAGIC, 0x4, struct lb_owner)
#define LB_STOP      _IOWR(LB_MAGIC, 0x5, struct lb_owner)
#define LB_RECOVERY  _IOWR(LB_MAGIC, 0x6, struct lb_owner)

#define PACKAGENAME_MAX_LEN  256

struct lb_data {
	int chr_major;
	struct class *chr_class;
	struct device *chr_dev;
	bool initialized;
	struct lb_collector *collector;
	struct lb_preread *preread;
};

struct lb_owner {
	unsigned int uid;
	int pid;
	char name[PACKAGENAME_MAX_LEN];
};

struct lb_pid_info {
	struct list_head list;
	int pid;
};

extern struct lb_data g_lb_data;

#endif /* _XRING_LB_DRV_H_ */
