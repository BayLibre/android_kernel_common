/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM mm

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_MM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_MM_H

#include <trace/hooks/vendor_hooks.h>

struct shmem_inode_info;
struct folio;
struct folio_batch;

DECLARE_RESTRICTED_HOOK(android_rvh_shmem_get_folio,
			TP_PROTO(struct shmem_inode_info *info, struct folio **folio),
			TP_ARGS(info, folio), 2);

/*

DECLARE_RESTRICTED_HOOK(android_rvh_set_skip_swapcache_flags,
			TP_PROTO(gfp_t *flags),
			TP_ARGS(flags), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_set_gfp_zone_flags,
			TP_PROTO(gfp_t *flags),
			TP_ARGS(flags), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_set_readahead_gfp_mask,
			TP_PROTO(gfp_t *flags),
			TP_ARGS(flags), 1);
*/
struct mem_cgroup;
DECLARE_HOOK(android_vh_mem_cgroup_alloc,
	TP_PROTO(struct mem_cgroup *memcg),
	TP_ARGS(memcg));
DECLARE_HOOK(android_vh_mem_cgroup_free,
	TP_PROTO(struct mem_cgroup *memcg),
	TP_ARGS(memcg));
DECLARE_HOOK(android_vh_io_statistics,
	TP_PROTO(struct address_space *mapping, unsigned int index,
		unsigned int nr_page, bool read, bool direct),
	TP_ARGS(mapping, index, nr_page, read, direct));

struct cma;
DECLARE_HOOK(android_vh_cma_alloc_bypass,
	TP_PROTO(struct cma *cma, unsigned long count, unsigned int align,
		gfp_t gfp_mask, struct page **page, bool *bypass),
	TP_ARGS(cma, count, align, gfp_mask, page, bypass));

struct compact_control;
DECLARE_HOOK(android_vh_isolate_freepages,
	TP_PROTO(struct compact_control *cc, struct page *page, bool *bypass),
	TP_ARGS(cc, page, bypass));

struct oom_control;
DECLARE_HOOK(android_vh_oom_check_panic,
	TP_PROTO(struct oom_control *oc, int *ret),
	TP_ARGS(oc, ret));

struct page_vma_mapped_walk;
DECLARE_HOOK(android_vh_slab_alloc_node,
	TP_PROTO(void *object, unsigned long addr, struct kmem_cache *s),
	TP_ARGS(object, addr, s));
DECLARE_HOOK(android_vh_slab_free,
	TP_PROTO(unsigned long addr, struct kmem_cache *s),
	TP_ARGS(addr, s));
DECLARE_HOOK(android_vh_test_clear_look_around_ref,
	TP_PROTO(struct page *page),
	TP_ARGS(page));
DECLARE_HOOK(android_vh_look_around_migrate_folio,
	TP_PROTO(struct folio *old_folio, struct folio *new_folio),
	TP_ARGS(old_folio, new_folio));
DECLARE_HOOK(android_vh_look_around,
	TP_PROTO(struct page_vma_mapped_walk *pvmw, struct folio *folio,
		struct vm_area_struct *vma, int *referenced),
	TP_ARGS(pvmw, folio, vma, referenced));

DECLARE_HOOK(android_vh_calc_alloc_flags,
	TP_PROTO(gfp_t gfp_mask, unsigned int *alloc_flags,
		bool *bypass),
	TP_ARGS(gfp_mask, alloc_flags, bypass));

DECLARE_HOOK(android_vh_slab_folio_alloced,
	TP_PROTO(unsigned int order, gfp_t flags),
	TP_ARGS(order, flags));
DECLARE_HOOK(android_vh_kmalloc_large_alloced,
	TP_PROTO(struct folio *folio, unsigned int order, gfp_t flags),
	TP_ARGS(folio, order, flags));
DECLARE_RESTRICTED_HOOK(android_rvh_ctl_dirty_rate,
	TP_PROTO(struct inode *inode),
	TP_ARGS(inode), 1);

DECLARE_HOOK(android_vh_alloc_pages_entry,
	TP_PROTO(gfp_t *gfp, unsigned int order, int preferred_nid,
		nodemask_t *nodemask),
	TP_ARGS(gfp, order, preferred_nid, nodemask));

<<<<<<< HEAD   (efec34 ANDROID: yukawa drops build_config.)
DECLARE_HOOK(android_vh_free_unref_folios_to_pcp_bypass,
	TP_PROTO(struct folio_batch *folios, bool *bypass),
	TP_ARGS(folios, bypass));
DECLARE_HOOK(android_vh_cma_alloc_fail,
	TP_PROTO(char *name, unsigned long count, unsigned long req_count),
	TP_ARGS(name, count, req_count));
DECLARE_RESTRICTED_HOOK(android_rvh_vmalloc_node_bypass,
	TP_PROTO(unsigned long size, gfp_t gfp_mask, void **addr),
	TP_ARGS(size, gfp_mask, addr), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_vfree_bypass,
	TP_PROTO(const void *addr, bool *bypass),
	TP_ARGS(addr, bypass), 1);
||||||| BASE
DECLARE_HOOK(android_vh_filemap_update_page,
	TP_PROTO(struct address_space *mapping, struct folio *folio,
		struct file *file),
	TP_ARGS(mapping, folio, file));
DECLARE_HOOK(android_vh_cma_debug_show_areas,
	TP_PROTO(bool *show),
	TP_ARGS(show));
DECLARE_HOOK(android_vh_alloc_contig_range_not_isolated,
	TP_PROTO(unsigned long start, unsigned end),
	TP_ARGS(start, end));
DECLARE_HOOK(android_vh_warn_alloc_tune_ratelimit,
	TP_PROTO(struct ratelimit_state *rs),
	TP_ARGS(rs));
DECLARE_HOOK(android_vh_warn_alloc_show_mem_bypass,
	TP_PROTO(bool *bypass),
	TP_ARGS(bypass));
DECLARE_HOOK(android_vh_free_pages_prepare_bypass,
	TP_PROTO(struct page *page, unsigned int order,
		int __bitwise flags, bool *skip_free_pages_prepare),
	TP_ARGS(page, order, flags, skip_free_pages_prepare));
DECLARE_HOOK(android_vh_free_pages_ok_bypass,
	TP_PROTO(struct page *page, unsigned int order,
		int __bitwise flags, bool *skip_free_pages_ok),
	TP_ARGS(page, order, flags, skip_free_pages_ok));
DECLARE_HOOK(android_vh_split_large_folio_bypass,
	TP_PROTO(bool *bypass),
	TP_ARGS(bypass));
DECLARE_HOOK(android_vh_do_read_fault,
	TP_PROTO(struct vm_fault *vmf, unsigned long fault_around_bytes),
	TP_ARGS(vmf, fault_around_bytes));
DECLARE_HOOK(android_vh_filemap_read,
	TP_PROTO(struct file *file, loff_t pos, size_t size),
	TP_ARGS(file, pos, size));
DECLARE_HOOK(android_vh_filemap_map_pages,
	TP_PROTO(struct file *file, pgoff_t first_pgoff,
		pgoff_t last_pgoff, vm_fault_t ret),
	TP_ARGS(file, first_pgoff, last_pgoff, ret));
DECLARE_HOOK(android_vh_page_cache_readahead_start,
	TP_PROTO(struct file *file, pgoff_t pgoff,
		unsigned int size, bool sync),
	TP_ARGS(file, pgoff, size, sync));
DECLARE_HOOK(android_vh_page_cache_readahead_end,
	TP_PROTO(struct file *file, pgoff_t pgoff),
	TP_ARGS(file, pgoff));
DECLARE_HOOK(android_vh_filemap_fault_start,
	TP_PROTO(struct file *file, pgoff_t pgoff),
	TP_ARGS(file, pgoff));
DECLARE_HOOK(android_vh_filemap_fault_end,
	TP_PROTO(struct file *file, pgoff_t pgoff),
	TP_ARGS(file, pgoff));

DECLARE_HOOK(android_vh_cma_alloc_set_max_retries,
	TP_PROTO(int *max_retries),
	TP_ARGS(max_retries));
DECLARE_HOOK(android_vh_compact_finished,
	TP_PROTO(bool *abort_compact),
	TP_ARGS(abort_compact));
DECLARE_HOOK(android_vh_madvise_cold_or_pageout_abort,
	TP_PROTO(struct vm_area_struct *vma, bool *abort_madvise),
	TP_ARGS(vma, abort_madvise));
DECLARE_HOOK(android_vh_zs_shrinker_adjust,
	TP_PROTO(unsigned long *pages_to_free),
	TP_ARGS(pages_to_free));
DECLARE_HOOK(android_vh_zs_shrinker_bypass,
	TP_PROTO(bool *bypass),
	TP_ARGS(bypass));
DECLARE_HOOK(android_vh_customize_thp_pcp_order,
	TP_PROTO(unsigned int *order),
	TP_ARGS(order));
DECLARE_HOOK(android_vh_customize_thp_gfp_orders,
	TP_PROTO(gfp_t *gfp_mask, unsigned long *orders, int *order),
	TP_ARGS(gfp_mask, orders, order));
DECLARE_HOOK(android_vh_init_adjust_zone_wmark,
	TP_PROTO(struct zone *zone, u64 interval),
	TP_ARGS(zone, interval));
DECLARE_HOOK(android_vh_do_group_exit,
	TP_PROTO(struct task_struct *tsk),
	TP_ARGS(tsk));
=======
DECLARE_HOOK(android_vh_filemap_update_page,
	TP_PROTO(struct address_space *mapping, struct folio *folio,
		struct file *file),
	TP_ARGS(mapping, folio, file));
DECLARE_HOOK(android_vh_cma_debug_show_areas,
	TP_PROTO(bool *show),
	TP_ARGS(show));
DECLARE_HOOK(android_vh_alloc_contig_range_not_isolated,
	TP_PROTO(unsigned long start, unsigned end),
	TP_ARGS(start, end));
DECLARE_HOOK(android_vh_warn_alloc_tune_ratelimit,
	TP_PROTO(struct ratelimit_state *rs),
	TP_ARGS(rs));
DECLARE_HOOK(android_vh_warn_alloc_show_mem_bypass,
	TP_PROTO(bool *bypass),
	TP_ARGS(bypass));
DECLARE_HOOK(android_vh_free_pages_prepare_bypass,
	TP_PROTO(struct page *page, unsigned int order,
		int __bitwise flags, bool *skip_free_pages_prepare),
	TP_ARGS(page, order, flags, skip_free_pages_prepare));
DECLARE_HOOK(android_vh_free_pages_ok_bypass,
	TP_PROTO(struct page *page, unsigned int order,
		int __bitwise flags, bool *skip_free_pages_ok),
	TP_ARGS(page, order, flags, skip_free_pages_ok));
DECLARE_HOOK(android_vh_split_large_folio_bypass,
	TP_PROTO(bool *bypass),
	TP_ARGS(bypass));
DECLARE_HOOK(android_vh_do_read_fault,
	TP_PROTO(struct vm_fault *vmf, unsigned long fault_around_bytes),
	TP_ARGS(vmf, fault_around_bytes));
DECLARE_HOOK(android_vh_filemap_read,
	TP_PROTO(struct file *file, loff_t pos, size_t size),
	TP_ARGS(file, pos, size));
DECLARE_HOOK(android_vh_filemap_map_pages,
	TP_PROTO(struct file *file, pgoff_t first_pgoff,
		pgoff_t last_pgoff, vm_fault_t ret),
	TP_ARGS(file, first_pgoff, last_pgoff, ret));
DECLARE_HOOK(android_vh_page_cache_readahead_start,
	TP_PROTO(struct file *file, pgoff_t pgoff,
		unsigned int size, bool sync),
	TP_ARGS(file, pgoff, size, sync));
DECLARE_HOOK(android_vh_page_cache_readahead_end,
	TP_PROTO(struct file *file, pgoff_t pgoff),
	TP_ARGS(file, pgoff));
DECLARE_HOOK(android_vh_filemap_fault_start,
	TP_PROTO(struct file *file, pgoff_t pgoff),
	TP_ARGS(file, pgoff));
DECLARE_HOOK(android_vh_filemap_fault_end,
	TP_PROTO(struct file *file, pgoff_t pgoff),
	TP_ARGS(file, pgoff));

DECLARE_HOOK(android_vh_cma_alloc_set_max_retries,
	TP_PROTO(int *max_retries),
	TP_ARGS(max_retries));
DECLARE_HOOK(android_vh_compact_finished,
	TP_PROTO(bool *abort_compact),
	TP_ARGS(abort_compact));
DECLARE_HOOK(android_vh_madvise_cold_or_pageout_abort,
	TP_PROTO(struct vm_area_struct *vma, bool *abort_madvise),
	TP_ARGS(vma, abort_madvise));
DECLARE_HOOK(android_vh_zs_shrinker_adjust,
	TP_PROTO(unsigned long *pages_to_free),
	TP_ARGS(pages_to_free));
DECLARE_HOOK(android_vh_zs_shrinker_bypass,
	TP_PROTO(bool *bypass),
	TP_ARGS(bypass));
DECLARE_HOOK(android_vh_customize_thp_pcp_order,
	TP_PROTO(unsigned int *order),
	TP_ARGS(order));
DECLARE_HOOK(android_vh_customize_thp_gfp_orders,
	TP_PROTO(gfp_t *gfp_mask, unsigned long *orders, int *order),
	TP_ARGS(gfp_mask, orders, order));
DECLARE_HOOK(android_vh_init_adjust_zone_wmark,
	TP_PROTO(struct zone *zone, u64 interval),
	TP_ARGS(zone, interval));
DECLARE_HOOK(android_vh_cma_alloc_retry,
	TP_PROTO(char *name, int *retry),
	TP_ARGS(name, retry));
DECLARE_HOOK(android_vh_do_group_exit,
	TP_PROTO(struct task_struct *tsk),
	TP_ARGS(tsk));
>>>>>>> CHANGE (e1f8da ANDROID: mm/cma: add vendor_hook in cma_alloc for retries)
#endif /* _TRACE_HOOK_MM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
