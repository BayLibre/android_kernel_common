// SPDX-License-Identifier: GPL-2.0
/*
 * Register read and write tracepoints
 *
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#include <linux/kallsyms.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/ftrace.h>
#include <linux/iorw.h>

#define CREATE_TRACE_POINTS
#include <trace/events/rwio.h>

#ifdef CONFIG_TRACEPOINTS
void __log_writeb_io(u8 val, volatile void __iomem *addr)
{
	trace_rwio_writeb(CALLER_ADDR0, val, addr);
}
EXPORT_SYMBOL_GPL(__log_writeb_io);
EXPORT_TRACEPOINT_SYMBOL_GPL(rwio_writeb);

void __log_writew_io(u16 val, volatile void __iomem *addr)
{
	trace_rwio_writew(CALLER_ADDR0, val, addr);
}
EXPORT_SYMBOL_GPL(__log_writew_io);
EXPORT_TRACEPOINT_SYMBOL_GPL(rwio_writew);

void __log_writel_io(u32 val, volatile void __iomem *addr)
{
	trace_rwio_writel(CALLER_ADDR0, val, addr);
}
EXPORT_SYMBOL_GPL(__log_writel_io);
EXPORT_TRACEPOINT_SYMBOL_GPL(rwio_writel);

void __log_writeq_io(u64 val, volatile void __iomem *addr)
{
	trace_rwio_writeq(CALLER_ADDR0, val, addr);
}
EXPORT_SYMBOL_GPL(__log_writeq_io);
EXPORT_TRACEPOINT_SYMBOL_GPL(rwio_writeq);

void __log_read_io(const volatile void __iomem *addr)
{
	trace_rwio_read(CALLER_ADDR0, addr);
}
EXPORT_SYMBOL_GPL(__log_read_io);
EXPORT_TRACEPOINT_SYMBOL_GPL(rwio_read);

void __log_post_readb_io(u8 val, const volatile void __iomem *addr)
{
	trace_rwio_post_readb(CALLER_ADDR0, val, addr);
}
EXPORT_SYMBOL_GPL(__log_post_readb_io);
EXPORT_TRACEPOINT_SYMBOL_GPL(rwio_post_readb);

void __log_post_readw_io(u16 val, const volatile void __iomem *addr)
{
	trace_rwio_post_readw(CALLER_ADDR0, val, addr);
}
EXPORT_SYMBOL_GPL(__log_post_readw_io);
EXPORT_TRACEPOINT_SYMBOL_GPL(rwio_post_readw);

void __log_post_readl_io(u32 val, const volatile void __iomem *addr)
{
	trace_rwio_post_readl(CALLER_ADDR0, val, addr);
}
EXPORT_SYMBOL_GPL(__log_post_readl_io);
EXPORT_TRACEPOINT_SYMBOL_GPL(rwio_post_readl);

void __log_post_readq_io(u64 val, const volatile void __iomem *addr)
{
	trace_rwio_post_readq(CALLER_ADDR0, val, addr);
}
EXPORT_SYMBOL_GPL(__log_post_readq_io);
EXPORT_TRACEPOINT_SYMBOL_GPL(rwio_post_readq);
#endif
