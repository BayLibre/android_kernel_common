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

#ifndef _XRING_LB_DEF_H_
#define _XRING_LB_DEF_H_

#include <linux/delay.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/bug.h>
#include <linux/printk.h>
#include "xring_lb_dbg.h"

extern int g_lb_msg_level;

#define XRING_LB_ERR(msg, ...) \
		do { if (g_lb_msg_level >= 0) \
			pr_notice("[LB E]:%s %d: "msg, __func__, __LINE__, ## __VA_ARGS__); \
		} while (0)

#define XRING_LB_WARN(msg, ...) \
		do { if (g_lb_msg_level >= 1) \
			pr_notice("[LB W]:%s %d: "msg, __func__, __LINE__, ## __VA_ARGS__); \
		} while (0)

#define XRING_LB_INFO(msg, ...) \
		do { if (g_lb_msg_level >= 2) \
			pr_notice("[LB I]:%s %d: "msg, __func__, __LINE__, ## __VA_ARGS__); \
		} while (0)

#define XRING_LB_DBG(msg, ...) \
		do { if (g_lb_msg_level >= 3) \
			pr_notice("[LB D]:%s %d: "msg, __func__, __LINE__, ## __VA_ARGS__); \
		} while (0)

#endif /* _XRING_LB_DEF_H_ */
