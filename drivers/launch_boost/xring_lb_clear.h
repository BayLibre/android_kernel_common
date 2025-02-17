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
#ifndef _XRING_LB_CLEAR_H_
#define _XRING_LB_CLEAR_H_

int xring_lb_unlink_one_record(const char *record_name);
void xring_lb_free_one_record(struct app_record *record);
void xring_lb_free_one_fileinfo(struct file_info *file_entry);
int xring_lb_dealwith_clear_cmd(unsigned long arg);

#endif
