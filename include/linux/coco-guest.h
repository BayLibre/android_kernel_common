/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _LINUX_COCO_GUEST_H
#define _LINUX_COCO_GUEST_H

struct page;

struct hypervisor_ops {
#ifdef CONFIG_MEMORY_RELINQUISH
	bool (*page_relinquish_disallowed)(void);
	void (*page_relinquish)(struct page *page);
#endif
};

extern struct hypervisor_ops hyp_ops;

#endif
