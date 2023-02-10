#ifndef __ARM64_KVM_HYPTRACE_H_
#define __ARM64_KVM_HYPTRACE_H_
#include <asm/kvm_hyp.h>

#include <linux/ring_buffer_ext.h>
#include <linux/trace_seq.h>
#include <linux/workqueue.h>

struct ht_iterator {
	struct ring_buffer_iter **buf_iter;
	struct hyp_entry_hdr *ent;
	struct trace_seq seq;
	struct list_head list;
	u64 ts;
	void *spare;
	size_t copy_leftover;
	size_t ent_size;
	struct delayed_work poke_work;
	unsigned long lost_events;
	cpumask_var_t cpus;
	int ent_cpu;
	int cpu;
};

/*
 * Host donations to the hypervisor to store the struct hyp_buffer_page.
 */
struct hyp_buffer_pages_backing {
	unsigned long start;
	size_t size;
};

struct hyp_trace_pack {
	struct hyp_buffer_pages_backing		backing;
	struct kvm_nvhe_clock_data		trace_clock_data;
	struct trace_buffer_pack		trace_buffer_pack;

};
#endif
