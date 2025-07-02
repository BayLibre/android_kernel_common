/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_COMPACTION_H
#define _LINUX_COMPACTION_H

/*
 * Determines how hard direct compaction should try to succeed.
 * Lower value means higher priority, analogically to reclaim priority.
 */
enum compact_priority {
	COMPACT_PRIO_SYNC_FULL,
	MIN_COMPACT_PRIORITY = COMPACT_PRIO_SYNC_FULL,
	COMPACT_PRIO_SYNC_LIGHT,
	MIN_COMPACT_COSTLY_PRIORITY = COMPACT_PRIO_SYNC_LIGHT,
	DEF_COMPACT_PRIORITY = COMPACT_PRIO_SYNC_LIGHT,
	COMPACT_PRIO_ASYNC,
	INIT_COMPACT_PRIORITY = COMPACT_PRIO_ASYNC
};

/* Return values for compact_zone() and try_to_compact_pages() */
/* When adding new states, please adjust include/trace/events/compaction.h */
enum compact_result {
	/* For more detailed tracepoint output - internal to compaction */
	COMPACT_NOT_SUITABLE_ZONE,
	/*
	 * compaction didn't start as it was not possible or direct reclaim
	 * was more suitable
	 */
	COMPACT_SKIPPED,
	/* compaction didn't start as it was deferred due to past failures */
	COMPACT_DEFERRED,

	/* For more detailed tracepoint output - internal to compaction */
	COMPACT_NO_SUITABLE_PAGE,
	/* compaction should continue to another pageblock */
	COMPACT_CONTINUE,

	/*
	 * The full zone was compacted scanned but wasn't successful to compact
	 * suitable pages.
	 */
	COMPACT_COMPLETE,
	/*
	 * direct compaction has scanned part of the zone but wasn't successful
	 * to compact suitable pages.
	 */
	COMPACT_PARTIAL_SKIPPED,

	/* compaction terminated prematurely due to lock contentions */
	COMPACT_CONTENDED,

	/*
	 * direct compaction terminated after concluding that the allocation
	 * should now succeed
	 */
	COMPACT_SUCCESS,
};

struct alloc_context; /* in mm/internal.h */

/*
 * Number of free order-0 pages that should be available above given watermark
 * to make sure compaction has reasonable chance of not running out of free
 * pages that it needs to isolate as migration target during its work.
 */
static inline unsigned long compact_gap(unsigned int order)
{
	/*
	 * Although all the isolations for migration are temporary, compaction
	 * free scanner may have up to 1 << order pages on its list and then
	 * try to split an (order - 1) free page. At that point, a gap of
	 * 1 << order might not be enough, so it's safer to require twice that
	 * amount. Note that the number of pages on the list is also
	 * effectively limited by COMPACT_CLUSTER_MAX, as that's the maximum
	 * that the migrate scanner can have isolated on migrate list, and free
	 * scanner is only invoked when the number of isolated free pages is
	 * lower than that. But it's not worth to complicate the formula here
	 * as a bigger gap for higher orders than strictly necessary can also
	 * improve chances of compaction success.
	 */
	return 2UL << order;
}

#define MTHP_ORDER 4

extern int mthp_page_percentage;
extern int mthp_compact_low;
extern int mthp_compact;
extern int mthp_compact_busy_threshold;
extern atomic_t mthp_compact_triggered;
extern atomic_long_t nr_free_mthp[MAX_NR_ZONES];
extern atomic_long_t nr_free_fraged[MAX_NR_ZONES];

#define MTHP_COMPACT_ENABLED	2

static inline int get_mthp_compact(void)
{
	return atomic_read(&mthp_compact_triggered);
}

static inline int test_and_set_mthp_compact(void)
{
	return atomic_cmpxchg(&mthp_compact_triggered, 0, 1);
}

bool __alloc_busy(int threshold);

static inline enum compact_result should_compact_zone(struct zone *zone)
{
	long low_threshold, nr_mthp_pages, nr_small_pages;

	if (mthp_compact != MTHP_COMPACT_ENABLED)
		return COMPACT_PARTIAL_SKIPPED;

	if (__alloc_busy(mthp_compact_busy_threshold))
		return COMPACT_PARTIAL_SKIPPED;

	nr_small_pages = atomic_long_read(&nr_free_fraged[zone_idx(zone)]);
	if (nr_small_pages < mthp_compact_low)
		return COMPACT_PARTIAL_SKIPPED;

	low_threshold = nr_small_pages / mthp_page_percentage;
	nr_mthp_pages = atomic_long_read(&nr_free_mthp[zone_idx(zone)]);

	return nr_mthp_pages < low_threshold ?  COMPACT_CONTINUE : COMPACT_SUCCESS;
}


#ifdef CONFIG_COMPACTION

extern unsigned int extfrag_for_order(struct zone *zone, unsigned int order);
extern int fragmentation_index(struct zone *zone, unsigned int order);
extern enum compact_result try_to_compact_pages(gfp_t gfp_mask,
		unsigned int order, unsigned int alloc_flags,
		const struct alloc_context *ac, enum compact_priority prio,
		struct page **page);
extern void reset_isolation_suitable(pg_data_t *pgdat);
extern bool compaction_suitable(struct zone *zone, int order,
					       int highest_zoneidx);

extern void compaction_defer_reset(struct zone *zone, int order,
				bool alloc_success);

bool compaction_zonelist_suitable(struct alloc_context *ac, int order,
					int alloc_flags);

extern void __meminit kcompactd_run(int nid);
extern void __meminit kcompactd_stop(int nid);
extern void wakeup_kcompactd(pg_data_t *pgdat, int order, int highest_zoneidx);
extern unsigned long isolate_and_split_free_page(struct page *page,
				struct list_head *list);
extern void compact_node_async(int nid);
#else
static inline void reset_isolation_suitable(pg_data_t *pgdat)
{
}

static inline bool compaction_suitable(struct zone *zone, int order,
						      int highest_zoneidx)
{
	return false;
}

static inline void kcompactd_run(int nid)
{
}
static inline void kcompactd_stop(int nid)
{
}

static inline void wakeup_kcompactd(pg_data_t *pgdat,
				int order, int highest_zoneidx)
{
}

static inline unsigned long isolate_and_split_free_page(struct page *page,
				struct list_head *list)
{
	return 0;
}

#endif /* CONFIG_COMPACTION */

struct node;
#if defined(CONFIG_COMPACTION) && defined(CONFIG_SYSFS) && defined(CONFIG_NUMA)
extern int compaction_register_node(struct node *node);
extern void compaction_unregister_node(struct node *node);

#else

static inline int compaction_register_node(struct node *node)
{
	return 0;
}

static inline void compaction_unregister_node(struct node *node)
{
}
#endif /* CONFIG_COMPACTION && CONFIG_SYSFS && CONFIG_NUMA */

#endif /* _LINUX_COMPACTION_H */
