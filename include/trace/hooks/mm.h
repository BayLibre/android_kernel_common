/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM mm

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_MM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_MM_H

#include <linux/types.h>

#include <linux/mm.h>
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

DECLARE_RESTRICTED_HOOK(android_rvh_set_skip_swapcache_flags,
			TP_PROTO(gfp_t *flags),
			TP_ARGS(flags), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_set_gfp_zone_flags,
			TP_PROTO(gfp_t *flags),
			TP_ARGS(flags), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_set_readahead_gfp_mask,
			TP_PROTO(gfp_t *flags),
			TP_ARGS(flags), 1);
DECLARE_HOOK(android_vh_rmqueue,
	TP_PROTO(struct zone *preferred_zone, struct zone *zone,
		unsigned int order, gfp_t gfp_flags,
		unsigned int alloc_flags, int migratetype),
	TP_ARGS(preferred_zone, zone, order,
		gfp_flags, alloc_flags, migratetype));
DECLARE_HOOK(android_vh_pagecache_get_page,
	TP_PROTO(struct address_space *mapping, pgoff_t index,
		int fgp_flags, gfp_t gfp_mask, struct page *page),
	TP_ARGS(mapping, index, fgp_flags, gfp_mask, page));
struct dma_heap;
DECLARE_HOOK(android_vh_dma_heap_buffer_alloc,
	TP_PROTO(struct dma_heap *heap, size_t len,
		unsigned int fd_flags, unsigned int heap_flags),
	TP_ARGS(heap, len, fd_flags, heap_flags));
struct dma_buf;
DECLARE_HOOK(android_vh_dma_heap_buffer_free,
	TP_PROTO(struct dma_buf *dmabuf),
	TP_ARGS(dmabuf));
/* macro versions of hooks are no longer required */

#endif /* _TRACE_HOOK_MM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
