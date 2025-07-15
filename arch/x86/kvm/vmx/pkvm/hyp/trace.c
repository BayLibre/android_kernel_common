/*
 * SPDX-License-Identifier: GPL-2.0
 * Copyright (C) 2022 Intel Corporation
 */
#include <linux/kvm_host.h>
#include <asm/pkvm_spinlock.h>
#include <asm/kvm_pkvm.h>
#include <pkvm.h>
#include "trace.h"
#include "debug.h"
#include "pkvm/pkvm.h"

/*
 * memset/memcpy can be re-defined by include/linux/fortify-string.h, which
 * may introduce additional linux kernel symbols. Undefine them to force
 * use the implementation in pkvm/hyp/lib/
 * */
#undef memset
#undef memcpy

struct perf_ctrl {
	int age;
	bool on;
};
static DEFINE_PER_CPU(struct vmexit_perf, hvcpu_perf);
static DEFINE_PER_CPU(struct perf_ctrl, perf_ctrl);

static inline unsigned long long pkvm_rdtsc_ordered(void)
{
	DECLARE_ARGS(val, low, high);

	asm volatile("lfence;rdtsc" : EAX_EDX_RET(val, low, high));

	return EAX_EDX_VAL(val, low, high);
}

static inline struct vmexit_perf *vcpu_to_perf(struct kvm_vcpu *vcpu)
{
	return (this_cpu_read(host_vcpu) == vcpu) ? this_cpu_ptr(&hvcpu_perf) :
						    &to_pkvm_vcpu(vcpu)->perf;
}

void trace_vmexit_start(struct kvm_vcpu *vcpu)
{
	struct perf_ctrl *pctrl = this_cpu_ptr(&perf_ctrl);
	struct vmexit_perf *perf;

	if (!pctrl->on)
		return;

	perf = vcpu_to_perf(vcpu);
	if (pctrl->age != perf->age) {
		perf->age = pctrl->age;
		memset(&perf->data.vmexit, 0, sizeof(struct vmexit_data));
		/*
		 * A host vcpu doesn't have vm_handle. Use CPU ID to tag
		 * its vcpu_id.
		 */
		if (!perf->data.vm_handle && (perf->data.vcpu_id != vcpu->cpu))
			perf->data.vcpu_id = vcpu->cpu;
	}

	perf->tsc = pkvm_rdtsc_ordered();
}

void trace_vmexit_end(struct kvm_vcpu *vcpu, u32 index)
{
	struct perf_ctrl *pctrl = this_cpu_ptr(&perf_ctrl);
	struct vmexit_perf *perf;
	unsigned long long cycles;

	if (!pctrl->on)
		return;

	perf = vcpu_to_perf(vcpu);
	if (pctrl->age != perf->age)
		return;

	if (index >= MAX_VMEXIT_REASONS)
		return;

	cycles = pkvm_rdtsc_ordered() - perf->tsc;

	pkvm_spin_lock(&perf->lock);
	perf->data.vmexit.cycles[index] += cycles;
	perf->data.vmexit.total_cycles += cycles;
	perf->data.vmexit.total_count++;
	perf->data.vmexit.reasons[index]++;
	pkvm_spin_unlock(&perf->lock);
}

void pkvm_handle_set_vmexit_trace(bool en)
{
	struct perf_ctrl *pctrl = this_cpu_ptr(&perf_ctrl);
	int cpu = raw_smp_processor_id();

	if (en && !pctrl->on) {
		if (++pctrl->age < 0)
			pctrl->age = 0;
		pctrl->on = true;
		pkvm_dbg("%s: CPU%d enable vmexit_trace\n", __func__, cpu);
		return;
	}

	if (!en && pctrl->on) {
		pctrl->on = false;
		pkvm_dbg("%s: CPU%d disable vmexit_trace\n", __func__, cpu);
		return;
	}
}

static void dump_vmexit_trace(struct perf_data *dump, struct vmexit_perf *perf)
{
	pkvm_spin_lock(&perf->lock);
	memcpy(dump, &perf->data, sizeof(struct perf_data));
	pkvm_spin_unlock(&perf->lock);
}

void pkvm_handle_dump_vmexit_trace(unsigned long pa, unsigned long size)
{
	struct vmexit_perf *perf;
	int cpu, i, j;
	void *out;

	if (!VALID_PAGE(pa))
		return;

	out = __pkvm_va(pa);

	/*
	 * Copy host_vcpu perf data first as this will be dumpped first by
	 * the host. Then copy the guest perf data.
	 */
	for_each_possible_cpu(cpu) {
		perf = per_cpu_ptr(&hvcpu_perf, cpu);
		if (size >= sizeof(struct perf_data)) {
			struct perf_data *dump = out;

			dump_vmexit_trace(dump, perf);
			out += sizeof(struct perf_data);
			size -= sizeof(struct perf_data);
		}
	}

	for (i = 0; i < MAX_PKVM_VMS; i++) {
		struct pkvm_vm *vm = get_pkvm_vm(idx_to_vm_handle(i));

		if (!vm)
			continue;

		for (j = 0; j < to_kvm(vm)->created_vcpus; j++) {
			perf = &vm->vcpus[j]->perf;

			if (size >= sizeof(struct perf_data)) {
				struct perf_data *dump = out;

				dump_vmexit_trace(dump, perf);
				out += sizeof(struct perf_data);
				size -= sizeof(struct perf_data);
			}
		}

		put_pkvm_vm(vm);
	}
}

void pkvm_vcpu_perf_init(struct kvm_vcpu *vcpu)
{
	struct vmexit_perf *perf = &to_pkvm_vcpu(vcpu)->perf;

	perf->data.vm_handle = vcpu->kvm->arch.pkvm.pkvm_vm_handle;
	perf->data.vcpu_id = vcpu->vcpu_id;
}
