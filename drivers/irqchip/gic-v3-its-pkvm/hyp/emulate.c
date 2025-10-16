// SPDX-License-Identifier: GPL-2.0-only
#include "emulate.h"
#include "module.h"

#include <asm/kvm_hyp.h>
#include <asm/kvm_pkvm_module.h>

LIST_HEAD(emulate_regions_list);

int hyp_add_emulate(struct emulate *emulate)
{
	int ret;

	if (!PAGE_ALIGNED(emulate->base) || !PAGE_ALIGNED(emulate->size) ||
	    !emulate->handler)
		return -EINVAL;

	ret = host_stage2_mod_prot(emulate->base >> PAGE_SHIFT, KVM_PGTABLE_PROT_DEVICE,
				   emulate->size >> PAGE_SHIFT, true);

	list_add(&emulate->list, &emulate_regions_list);

	return ret;
}

static int handle_emulate(struct emulate *emulate, struct user_pt_regs *regs,
			  u64 esr, u64 addr)
{
	unsigned int len;
	u64 offset, reg_value;
	int reg, ret;
	bool write;

	len = BIT((esr & ESR_ELx_SAS) >> ESR_ELx_SAS_SHIFT);
	if (len != sizeof(u32) && len != sizeof(u64))
		return -EFAULT;

	reg = (esr & ESR_ELx_SRT_MASK) >> ESR_ELx_SRT_SHIFT;
	write = (esr & ESR_ELx_WNR) == ESR_ELx_WNR;
	offset = addr - emulate->base;

	reg_value = write ? regs->regs[reg] : 0xdeadbeefdeadbeef;
	reg_value &= GENMASK_ULL((len << 3) - 1, 0);

	ret = emulate->handler(emulate, offset, write, &reg_value, len);
	if (ret)
		return ret;

	if (!write) {
		reg_value &= BIT(len << 3) - 1;
		regs->regs[reg] = reg_value;
	}

	write_sysreg_el2(read_sysreg_el2(SYS_ELR) + 4, SYS_ELR);

	return 0;
}

int hyp_emulate_perm_fault(struct user_pt_regs *regs, u64 esr, u64 addr)
{
	struct emulate *emulate;

	list_for_each_entry(emulate, &emulate_regions_list, list) {
		if (addr < emulate->base ||
		    addr >= emulate->base + emulate->size)
			continue;

		return handle_emulate(emulate, regs, esr, addr);
	}

	return -EFAULT;
}

#ifdef CONFIG_LIST_HARDENED
bool __list_add_valid_or_report(struct list_head *new,
				struct list_head *prev,
				struct list_head *next)
{
	return CALL_FROM_OPS(list_add_valid_or_report, new, prev, next);
}

bool __list_del_entry_valid_or_report(struct list_head *entry)
{
	return CALL_FROM_OPS(list_del_entry_valid_or_report, entry);
}
#endif
