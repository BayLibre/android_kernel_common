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

#undef TRACE_SYSTEM
#define TRACE_SYSTEM xring_lb

#if !defined(_TRACE_EVENT_XRING_LB_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_EVENT_XRING_LB_H

#include <linux/tracepoint.h>
#include "xring_lb_collect.h"
#include "xring_lb_drv.h"
#include "xring_lb_clear.h"
#include "xring_lb_flush.h"

TRACE_EVENT(xring_lb_preread,
	TP_PROTO(struct file_info *finfo, unsigned int start, unsigned int end),
	TP_ARGS(finfo, start, end),

	TP_STRUCT__entry(
		__field(struct file_info *, finfo)
		__field(unsigned int, start)
		__field(unsigned int, end)
	),

	TP_fast_assign(
		__entry->finfo = finfo;
		__entry->start = start;
		__entry->end = end;
	),

	TP_printk("file: %s, start: %d, end: %d, size: %d",
			__entry->finfo->path,
			__entry->start,
			__entry->end,
			__entry->end - __entry->start)
);

TRACE_EVENT(xring_lb_collect,
	TP_PROTO(struct file_info *finfo, unsigned int start, unsigned int end),
	TP_ARGS(finfo, start, end),

	TP_STRUCT__entry(
		__field(struct file_info *, finfo)
		__field(unsigned int, start)
		__field(unsigned int, end)
	),

	TP_fast_assign(
		__entry->finfo = finfo;
		__entry->start = start;
		__entry->end = end;
	),

	TP_printk("file: %s, start: %d, end: %d, size: %d",
			__entry->finfo->path,
			__entry->start,
			__entry->end,
			__entry->end - __entry->start)
);

#endif

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE xring_lb_trace
#include <trace/define_trace.h>
