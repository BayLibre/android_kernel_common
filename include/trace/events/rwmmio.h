/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM rwmmio

#if !defined(_TRACE_MMIO_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MMIO_H

#include <linux/tracepoint.h>

TRACE_EVENT(rwmmio_write,

	TP_PROTO(unsigned long fn, u64 val, u8 width, volatile void __iomem *addr),

	TP_ARGS(fn, val, width, addr),

	TP_STRUCT__entry(
		__field(u64, fn)
		__field(u64, val)
		__field(u8, width)
		__field(u64, addr)
	),

	TP_fast_assign(
		__entry->fn = fn;
		__entry->val = val;
		__entry->width = width;
		__entry->addr = (u64)addr;
	),

	TP_printk("%pS write val=0x%llx in addr=%llx of width=%x\n",
		__entry->fn, __entry->val, __entry->addr, __entry->width)
);

TRACE_EVENT(rwmmio_read,

	TP_PROTO(unsigned long fn, const volatile void __iomem *addr),

	TP_ARGS(fn, addr),

	TP_STRUCT__entry(
		__field(u64, fn)
		__field(u64, addr)
	),

	TP_fast_assign(
		__entry->fn = fn;
		__entry->addr = (u64)addr;
	),

	TP_printk("%pS read addr=%llx\n", __entry->fn, __entry->addr)
);

TRACE_EVENT(rwmmio_post_read,

	TP_PROTO(unsigned long fn, u64 val, u8 width, const volatile void __iomem *addr),

	TP_ARGS(fn, val, width, addr),

	TP_STRUCT__entry(
		__field(u64, fn)
		__field(u64, val)
		__field(u8, width)
		__field(u64, addr)
	),

	TP_fast_assign(
		__entry->fn = fn;
		__entry->val = val;
		__entry->width = width;
		__entry->addr = (u64)addr;
	),

	TP_printk("%pS read val=0x%llx from addr=%llx of width=%x\n",
		 __entry->fn, __entry->val, __entry->addr, __entry->width)
);

#endif /* _TRACE_MMIO_H */

#include <trace/define_trace.h>
