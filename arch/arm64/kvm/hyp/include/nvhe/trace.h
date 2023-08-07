#ifndef __ARM64_KVM_HYP_NVHE_TRACE_H
#define __ARM64_KVM_HYP_NVHE_TRACE_H

#include <nvhe/trace.h>

#include <linux/trace_events.h>
#include <linux/ring_buffer.h>

#include <asm/kvm_hyptrace.h>
#include <asm/kvm_hypevents_defs.h>
#include <asm/percpu.h>

#ifdef CONFIG_TRACING

struct hyp_buffer_page {
	struct list_head list;
	struct buffer_data_page *page;
	atomic_t write;
	atomic_t entries;
};

#define HYP_RB_NONWRITABLE	0
#define HYP_RB_WRITABLE		1
#define HYP_RB_WRITING		2

struct hyp_rb_per_cpu {
	struct hyp_buffer_page *tail_page;
	struct hyp_buffer_page *reader_page;
	struct hyp_buffer_page *head_page;
	struct hyp_buffer_page *bpages;
	unsigned long nr_pages;
	atomic64_t write_stamp;
	atomic_t pages_touched;
	atomic_t nr_entries;
	atomic_t status;
	atomic_t overrun;
};

static inline bool __start_write_hyp_rb(struct hyp_rb_per_cpu *rb)
{
	return atomic_cmpxchg(&rb->status, HYP_RB_WRITABLE, HYP_RB_WRITING)
		!= HYP_RB_NONWRITABLE;
}

static inline void __stop_write_hyp_rb(struct hyp_rb_per_cpu *rb)
{
	/*
	 * Paired with rb_cpu_disable()
	 */
	atomic_set_release(&rb->status, HYP_RB_WRITABLE);
}

struct hyp_rb_per_cpu;
DECLARE_PER_CPU(struct hyp_rb_per_cpu, trace_rb);

void *rb_reserve_trace_entry(struct hyp_rb_per_cpu *cpu_buffer, unsigned long length);

int __pkvm_load_tracing(unsigned long pack_va, size_t pack_size);
void __pkvm_teardown_tracing(void);
int __pkvm_enable_tracing(bool enable);
int __pkvm_rb_swap_reader_page(int cpu);
int __pkvm_rb_update_footers(int cpu);
int __pkvm_enable_event(unsigned short id, bool enable);

#define HYP_EVENT(__name, __proto, __struct, __assign, __printk)		\
	HYP_EVENT_FORMAT(__name, __struct);					\
	extern atomic_t __name##_enabled;					\
	extern unsigned short hyp_event_id_##__name;				\
	static inline void trace_##__name(__proto)				\
	{									\
		size_t length = sizeof(struct trace_hyp_format_##__name);	\
		struct hyp_rb_per_cpu *rb = this_cpu_ptr(&trace_rb);		\
		struct trace_hyp_format_##__name *__entry;			\
										\
		if (!atomic_read(&__name##_enabled))				\
			return;							\
		if (!__start_write_hyp_rb(rb))					\
			return;							\
		__entry = rb_reserve_trace_entry(rb, length);			\
		__entry->hdr.id = hyp_event_id_##__name;			\
		__assign							\
		__stop_write_hyp_rb(rb);					\
	}

/* TODO: atomic_t to static_branch */

static inline u8 hyp_printk_fmt_to_id(const char *fmt)
{
	const struct hyp_printk_fmt *ht_fmt =
		(const struct hyp_printk_fmt *)fmt;

	return ht_fmt->id;
}

#define __trace_hyp_printk(__fmt, a, b, c, d)		\
do {							\
	static struct hyp_printk_fmt ht_fmt		\
		__section(".hyp.printk_fmts") __used = {\
			.fmt = __fmt,			\
	};						\
	trace___hyp_printk(ht_fmt.fmt, a, b, c, d);	\
} while (0)

#define __trace_hyp_printk_0(fmt, arg)		\
	__trace_hyp_printk(fmt, 0, 0, 0, 0)
#define __trace_hyp_printk_1(fmt, a)		\
	__trace_hyp_printk(fmt, a, 0, 0, 0)
#define __trace_hyp_printk_2(fmt, a, b)		\
	__trace_hyp_printk(fmt, a, b, 0, 0)
#define __trace_hyp_printk_3(fmt, a, b, c)	\
	__trace_hyp_printk(fmt, a, b, c, 0)
#define __trace_hyp_printk_4(fmt, a, b, c, d) \
	__trace_hyp_printk(fmt, a, b, c, d)

#define __trace_hyp_printk_N(fmt, ...) \
	CONCATENATE(__trace_hyp_printk_, COUNT_ARGS(__VA_ARGS__))(fmt, ##__VA_ARGS__)

#define trace_hyp_printk(fmt, ...) \
	__trace_hyp_printk_N(fmt, __VA_ARGS__)
#else
static inline int __pkvm_load_tracing(unsigned long pack_va, size_t pack_size)
{
	return -ENODEV;
}

static inline void __pkvm_teardown_tracing(void) { }

static inline int __pkvm_enable_tracing(bool enable) { return -ENODEV; }

static inline int __pkvm_rb_swap_reader_page(int cpu)
{
	return -ENODEV;
}

static inline int __pkvm_rb_update_footers(int cpu)
{
	return -ENODEV;
}

#define HYP_EVENT(__name, __proto, __struct, __assign, __printk)	\
	static inline void trace_##__name(__proto) {}

static inline int __pkvm_enable_event(unsigned short id, bool enable)
{
	return -ENODEV;
}

#define trace_hyp_printk(fmt, ...)
#endif
#endif
