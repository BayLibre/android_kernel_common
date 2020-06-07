/* SPDX-License-Identifier: GPL-2.0 */
/*
 *
 */
#ifndef __MMIORW_H__
#define __MMIORW_H__

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/tracepoint-defs.h>

#if IS_ENABLED(CONFIG_TRACE_MMIO_ACCESS) && IS_ENABLED(__FTRACE_ENABLED_HERE__)
DECLARE_TRACEPOINT(rwmmio_write);
DECLARE_TRACEPOINT(rwmmio_read);
DECLARE_TRACEPOINT(rwmmio_post_read);

void __log_write_mmio(u64 val, u8 width, volatile void __iomem *addr);
void __log_read_mmio(const volatile void __iomem *addr);
void __log_post_read_mmio(u64 val, u8 width, const volatile void __iomem *addr);

#define log_write_mmio(val, width, addr)			\
do {							\
	if (tracepoint_enabled(rwmmio_write))		\
		__log_write_mmio(val, width, addr);	\
} while (0)

#define log_read_mmio(addr)				\
do {							\
	if (tracepoint_enabled(rwmmio_read))		\
		__log_read_mmio(addr);			\
} while (0)

#define log_post_read_mmio(val, width, addr)		\
do {							\
	if (tracepoint_enabled(rwmmio_post_read))	\
		__log_post_read_mmio(val, addr);		\
} while (0)

#else
static inline void log_write_mmio(u64 val, u8 width, volatile void __iomem *addr)
{ }
static inline void log_read_mmio(const volatile void __iomem *addr)
{ }
static inline void log_post_read_mmio(u64 val, u8 width, const volatile void __iomem *addr)
{ }
#endif /* CONFIG_TRACE_MMIO_ACCESS */

#endif /* ___MMIORW_H__  */
