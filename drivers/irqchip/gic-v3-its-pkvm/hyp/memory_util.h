// SPDX-License-Identifier: GPL-2.0-only
#ifndef __GIC_V3_ITS_PKVM_MEMORY_UTIL__
#define __GIC_V3_ITS_PKVM_MEMORY_UTIL__

#include "emulate.h"

#include <nvhe/spinlock.h>
#include <linux/list.h>
#include <linux/types.h>

struct tracked_region {
	phys_addr_t start;
	phys_addr_t end;
	u64 refcnt;

	void *priv;
	struct list_head list;
};

int share_pin_range(phys_addr_t start, phys_addr_t end);
int unshare_unpin_range(phys_addr_t start, phys_addr_t end);

struct region_tracker_ops {
	int (*init)(struct tracked_region *reg);
	int (*free)(struct tracked_region *reg);
	void *priv;
};

extern struct region_tracker_ops region_tracker_shared_ops;

struct region_tracker {
	const struct region_tracker_ops *ops;
	struct list_head regions_head;
	/* TODO: Check if actually needed */
	hyp_spinlock_t lock;
};

#define DEFINE_MEM_TRACKER(__name, __ops)                            \
	struct region_tracker __name = {                             \
		.ops = (__ops),                                      \
		.regions_head = LIST_HEAD_INIT(__name.regions_head), \
		.lock = __HYP_SPIN_LOCK_UNLOCKED,                    \
	}

int region_tracker_inc(struct region_tracker *tracker, phys_addr_t start,
		       phys_addr_t end);
int region_tracker_dec(struct region_tracker *tracker, phys_addr_t start,
		       phys_addr_t end);

#endif /* __GIC_V3_ITS_PKVM_MEMORY_UTIL__ */
