// SPDX-License-Identifier: GPL-2.0-only
#include "module.h"
#include "memory_util.h"

#include <nvhe/spinlock.h>

int share_pin_range(phys_addr_t start, phys_addr_t end)
{
	int ret;
	u64 pfn, pfn_end;
	void *virt_begin, *virt_end;

	hyp_puts(__func__);
	hyp_putx64(start);

	pfn = start >> PAGE_SHIFT;
	pfn_end = end >> PAGE_SHIFT;

	if (pfn == pfn_end)
		return -EINVAL;

	while (pfn < pfn_end) {
		ret = host_share_hyp(pfn);
		if (ret) {
			hyp_puts("Failed to share");
			goto unshare;
		}

		pfn++;
	}

	virt_begin = hyp_phys_to_virt(ALIGN_DOWN(start, PAGE_SIZE));
	virt_end = hyp_phys_to_virt(ALIGN(end, PAGE_SIZE));
	ret = hyp_pin_shared_mem(virt_begin, virt_end);
	if (ret)
		hyp_puts("Failed to pin");

	if (!ret)
		return 0;
unshare:
	for (pfn = pfn - 1; pfn >= (start >> PAGE_SHIFT); pfn--)
		WARN_ON(host_unshare_hyp(pfn));

	return ret;
}

int unshare_unpin_range(phys_addr_t start, phys_addr_t end)
{
	u64 pfn, pfn_end;
	void *virt_start, *virt_end;

	hyp_puts(__func__);
	hyp_putx64(start);

	virt_start = hyp_phys_to_virt(ALIGN_DOWN(start, PAGE_SIZE));
	virt_end = hyp_phys_to_virt(ALIGN(end, PAGE_SIZE));
	hyp_unpin_shared_mem(virt_start, virt_end);

	pfn = start >> PAGE_SHIFT;
	pfn_end = end >> PAGE_SHIFT;

	while (pfn < pfn_end) {
		WARN_ON(host_unshare_hyp(pfn));
		pfn++;
	}

	return 0;
}

static int shared_init(struct tracked_region *reg)
{
	return share_pin_range(reg->start, reg->end);
}

static int shared_free(struct tracked_region *reg)
{
	return unshare_unpin_range(reg->start, reg->end);
}

struct region_tracker_ops region_tracker_shared_ops = {
	.init = shared_init,
	.free = shared_free,
	.priv = NULL,
};

static int donated_init(struct tracked_region *reg)
{
	return host_donate_hyp(reg->start >> PAGE_SHIFT,
			       (reg->end - reg->start) >> PAGE_SHIFT);
}

static int donated_free(struct tracked_region *reg)
{
	return hyp_donate_host(reg->start >> PAGE_SHIFT,
			       (reg->end - reg->start) >> PAGE_SHIFT);
}

struct region_tracker_ops region_tracker_donated_ops = {
	.init = donated_init,
	.free = donated_free,
	.priv = NULL,
};

#define for_each_region(__tracker, __reg) \
	list_for_each_entry(__reg, &(__tracker)->regions_head, list)

static inline bool overlaps_exclusive(u64 start0, u64 end0, u64 start1,
				      u64 end1)
{
	return start0 < end1 && end0 > start1;
}

static inline struct tracked_region *
find_overlap(struct region_tracker *tracker, u64 start, u64 end)
{
	struct tracked_region *reg;

	for_each_region(tracker, reg) {
		if (overlaps_exclusive(start, end, reg->start, reg->end))
			return reg;
	}

	return NULL;
}

static inline int find(struct region_tracker *tracker, u64 start, u64 end,
		       struct tracked_region **reg)
{
	struct tracked_region *region;

	*reg = NULL;

	region = find_overlap(tracker, start, end);
	if (!region)
		return 0;

	if (region->start != start || region->end != end)
		return -EINVAL;

	*reg = region;
	return 0;
}

int region_tracker_inc(struct region_tracker *tracker, phys_addr_t start,
		       phys_addr_t end)
{
	struct tracked_region *reg = NULL;
	int ret;

	hyp_puts(__func__);
	hyp_putx64(start);
	hyp_spin_lock(&tracker->lock);

	ret = find(tracker, start, end, &reg);
	if (ret)
		goto exit;

	if (reg) {
		reg->refcnt++;
		goto exit;
	}

	reg = hyp_alloc(sizeof(*reg));
	if (!reg)
		return -ENOMEM;

	*reg = (struct tracked_region){
		.start = start,
		.end = end,
		.refcnt = 1,
		.priv = NULL,
	};

	ret = tracker->ops->init(reg);
	if (ret) {
		hyp_free(reg);
		goto exit;
	}

	list_add(&reg->list, &tracker->regions_head);
exit:
	hyp_spin_unlock(&tracker->lock);
	return ret;
}

int region_tracker_dec(struct region_tracker *tracker, phys_addr_t start,
		       phys_addr_t end)
{
	struct tracked_region *reg = NULL;
	int ret;

	hyp_puts(__func__);
	hyp_putx64(start);
	hyp_spin_lock(&tracker->lock);

	ret = find(tracker, start, end, &reg);
	if (ret || !reg) {
		if (!ret)
			ret = -EINVAL;
		goto exit;
	}

	reg->refcnt--;
	if (reg->refcnt > 0)
		goto exit;

	ret = tracker->ops->free(reg);
	hyp_free(reg);
	list_del(&reg->list);

exit:
	hyp_spin_unlock(&tracker->lock);
	return ret;
}
