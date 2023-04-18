// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 MediaTek Inc.
 */

#include <linux/irqchip/arm-gic-v3.h>
#include <kvm/arm_vgic.h>

#include <linux/gzvm.h>
#include <linux/gzvm_drv.h>
#include "gzvm_arch.h"

static bool lr_signals_eoi(uint64_t lr_val)
{
	return !(lr_val & ICH_LR_STATE) && (lr_val & ICH_LR_EOI) &&
		 !(lr_val & ICH_LR_HW);
}

/**
 * gzvm_sync_vgic_state() - Check all LRs synced from gz hypervisor.
 * @vcpu: Pointer to gzvm_vcpu.
 *
 * Traverse all LRs, see if any EOIed vint, notify_acked_irq if any.
 * GZ does not fold/unfold everytime KVM_RUN, so we have to traverse all saved
 * LRs. It will not takes much more time comparing to fold/unfold everytime
 * GZVM_RUN, because there are only few LRs.
 */
void gzvm_sync_vgic_state(struct gzvm_vcpu *vcpu)
{
	int i;

	for (i = 0; i < vcpu->hwstate->nr_lrs; i++) {
		u32 vintid;
		u64 lr_val = vcpu->hwstate->lr[i];
		/* 0 means unused */
		if (!lr_val)
			continue;

		vintid = lr_val & ICH_LR_VIRTUAL_ID_MASK;
		if (lr_signals_eoi(lr_val))
			gzvm_notify_acked_irq(vcpu->gzvm, vintid - VGIC_NR_PRIVATE_IRQS);
	}
}

/* is_irq_valid() - Check the irq number and irq_type are matched */
static bool is_irq_valid(u32 irq, u32 irq_type)
{
	switch (irq_type) {
	case GZVM_IRQ_TYPE_CPU:
		/*  0 ~ 15: SGI */
		if (likely(irq <= GZVM_IRQ_CPU_FIQ))
			return true;
		break;
	case GZVM_IRQ_TYPE_PPI:
		/* 16 ~ 31: PPI */
		if (likely(irq >= VGIC_NR_SGIS && irq < VGIC_NR_PRIVATE_IRQS))
			return true;
		break;
	case GZVM_IRQ_TYPE_SPI:
		/* 32 ~ : SPT */
		if (likely(irq >= VGIC_NR_PRIVATE_IRQS))
			return true;
		break;
	default:
		return false;
	}
	return false;
}

/**
 * gzvm_vgic_inject_irq() - Inject virtual interrupt to a VM
 * @vcpu_idx: vcpu index, only valid if PPI
 * @irq: irq number
 * @level: 1 if true else 0
 */
int gzvm_vgic_inject_irq(struct gzvm *gzvm, unsigned int vcpu_idx, u32 irq_type,
			 u32 irq, bool level)
{
	unsigned long a1 = assemble_vm_vcpu_tuple(gzvm->vm_id, vcpu_idx);
	struct arm_smccc_res res;

	if (!unlikely(is_irq_valid(irq, irq_type)))
		return -EINVAL;

	gzvm_hypcall_wrapper(MT_HVC_GZVM_IRQ_LINE, a1, irq, level,
			     0, 0, 0, 0, &res);
	if (res.a0) {
		pr_err("Failed to set IRQ level (%d) to irq#%u on vcpu %d with ret=%d\n",
		       level, irq, vcpu_idx, (int)res.a0);
		return -EFAULT;
	}

	return 0;
}

/**
 * gzvm_vgic_inject_spi() - Inject virtual spi interrupt
 *
 * @spi_irq: This is spi interrupt number (starts from 0 instead of 32)
 *
 * Return 0 if succeed else other negative values indicating each errors
 */
int gzvm_vgic_inject_spi(struct gzvm *gzvm, unsigned int vcpu_idx,
			 u32 spi_irq, bool level)
{
	return gzvm_vgic_inject_irq(gzvm, 0, GZVM_IRQ_TYPE_SPI,
				    spi_irq + VGIC_NR_PRIVATE_IRQS, level);
}
