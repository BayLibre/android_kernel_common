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
#ifndef _XRING_LB_FLUSH_H_
#define _XRING_LB_FLUSH_H_

struct lb_flush_header {
	unsigned int len;
	unsigned int crc;
	unsigned int record_disk_size;
};

#define MAX_PATH_LEN 30
extern char s_path_user0[MAX_PATH_LEN];

int xring_lb_dealwith_flush_cmd(unsigned long arg);
int xring_lb_dealwith_recovery_cmd(unsigned long arg);

#endif
