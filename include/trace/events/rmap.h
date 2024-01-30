/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM rmap

#if !defined(_TRACE_RMAP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RMAP_H

#include <linux/types.h>
#include <linux/tracepoint.h>
#include <linux/mm.h>


TRACE_EVENT(mm_rmap_page_referenced,
	TP_PROTO(ktime_t nsecs, int64_t count),

	TP_ARGS(nsecs, count),

	TP_STRUCT__entry(
		__field(ktime_t,	nsecs)
		__field(int64_t,	count)
	),

	TP_fast_assign(
		__entry->nsecs	= nsecs;
		__entry->count	= count;
	),

	TP_printk("nsecs=%lld count=%lld", __entry->nsecs, __entry->count)
);

#endif /* _TRACE_RMAP_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
