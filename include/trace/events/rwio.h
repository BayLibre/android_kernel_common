/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM rwio

#if !defined(_TRACE_RWIO_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RWIO_H

#include <linux/tracepoint.h>

TRACE_EVENT(rwio_writeb,

	TP_PROTO(unsigned long fn, u8 val, volatile void __iomem *addr),

	TP_ARGS(fn, val, addr),

	TP_STRUCT__entry(
		__field(u64, fn)
		__field(u8, val)
		__field(u64, addr)
	),

	TP_fast_assign(
		__entry->fn = fn;
		__entry->val = val;
		__entry->addr = (u64)addr;
	),

	TP_printk("%pS writeb val=0x%x in addr=%llx\n", __entry->fn, __entry->val, __entry->addr)
);

TRACE_EVENT(rwio_writew,

	TP_PROTO(unsigned long fn, u16 val, volatile void __iomem *addr),

	TP_ARGS(fn, val, addr),

	TP_STRUCT__entry(
		__field(u64, fn)
		__field(u16, val)
		__field(u64, addr)
	),

	TP_fast_assign(
		__entry->fn = fn;
		__entry->val = val;
		__entry->addr = (u64)addr;
	),

	TP_printk("%pS writew val=0x%x in addr=%llx\n", __entry->fn, __entry->val, __entry->addr)
);

TRACE_EVENT(rwio_writel,

	TP_PROTO(unsigned long fn, u32 val, volatile void __iomem *addr),

	TP_ARGS(fn, val, addr),

	TP_STRUCT__entry(
		__field(u64, fn)
		__field(u32, val)
		__field(u64, addr)
	),

	TP_fast_assign(
		__entry->fn = fn;
		__entry->val = val;
		__entry->addr = (u64)addr;
	),

	TP_printk("%pS writel val=0x%x in addr=%llx\n", __entry->fn, __entry->val, __entry->addr)
);

TRACE_EVENT(rwio_writeq,

	TP_PROTO(unsigned long fn, u64 val, volatile void __iomem *addr),

	TP_ARGS(fn, val, addr),

	TP_STRUCT__entry(
		__field(u64, fn)
		__field(u64, val)
		__field(u64, addr)
	),

	TP_fast_assign(
		__entry->fn = fn;
		__entry->val = val;
		__entry->addr = (u64)addr;
	),

	TP_printk("%pS writeq val=0x%llx in addr=%llx\n", __entry->fn, __entry->val, __entry->addr)
);

TRACE_EVENT(rwio_read,

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

TRACE_EVENT(rwio_post_readb,

	TP_PROTO(unsigned long fn, u8 val, const volatile void __iomem *addr),

	TP_ARGS(fn, val, addr),

	TP_STRUCT__entry(
		__field(u64, fn)
		__field(u8, val)
		__field(u64, addr)
	),

	TP_fast_assign(
		__entry->fn = fn;
		__entry->val = val;
		__entry->addr = (u64)addr;
	),

	TP_printk("%pS readb val=0x%x from addr=%llx\n", __entry->fn, __entry->val, __entry->addr)
);

TRACE_EVENT(rwio_post_readw,

	TP_PROTO(unsigned long fn, u16 val, const volatile void __iomem *addr),

	TP_ARGS(fn, val, addr),

	TP_STRUCT__entry(
		__field(u64, fn)
		__field(u16, val)
		__field(u64, addr)
	),

	TP_fast_assign(
		__entry->fn = fn;
		__entry->val = val;
		__entry->addr = (u64)addr;
	),

	TP_printk("%pS readw val=0x%x from addr=%llx\n", __entry->fn, __entry->val, __entry->addr)
);

TRACE_EVENT(rwio_post_readl,

	TP_PROTO(unsigned long fn, u32 val, const volatile void __iomem *addr),

	TP_ARGS(fn, val, addr),

	TP_STRUCT__entry(
		__field(u64, fn)
		__field(u32, val)
		__field(u64, addr)
	),

	TP_fast_assign(
		__entry->fn = fn;
		__entry->val = val;
		__entry->addr = (u64)addr;
	),

	TP_printk("%pS readl val=0x%x from addr=%llx\n", __entry->fn, __entry->val, __entry->addr)
);

TRACE_EVENT(rwio_post_readq,

	TP_PROTO(unsigned long fn, u64 val, const volatile void __iomem *addr),

	TP_ARGS(fn, val, addr),

	TP_STRUCT__entry(
		__field(u64, fn)
		__field(u64, val)
		__field(u64, addr)
	),

	TP_fast_assign(
		__entry->fn = fn;
		__entry->val = val;
		__entry->addr = (u64)addr;
	),

	TP_printk("%pS readq val=0x%llx from addr=%llx\n", __entry->fn, __entry->val, __entry->addr)
);

#endif /* _TRACE_PREEMPTIRQ_H */

#include <trace/define_trace.h>
