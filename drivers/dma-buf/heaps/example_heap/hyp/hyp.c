//SPDX-License-Identifier: GPL-2.0

#include "asm-generic/int-ll64.h"
#include "asm/kvm_pgtable.h"
#include "linux/arm-smccc.h"
#include "linux/cache.h"
#include "linux/compiler.h"
#include "linux/overflow.h"
#include "linux/types.h"
#include <asm/kvm_hyp.h>
#include <asm/kvm_pkvm_module.h>
#include "linux/list.h"

#include "example_heap.h"

static const struct pkvm_module_ops *ops;

#define MEMSHARE_TOKEN_SIZE 64

static struct module_heap_config vm_state;
struct example_heap_config ex_heap_config;

static u64 constant_time_token_neq(const struct arm_smccc_1_2_regs* regs, const u64* exp)
{
	u64 diff = 0;

	diff |= regs->a3  ^ exp[0];
	OPTIMIZER_HIDE_VAR(diff);
	diff |= regs->a4  ^ exp[1];
	OPTIMIZER_HIDE_VAR(diff);
	diff |= regs->a5  ^ exp[2];
	OPTIMIZER_HIDE_VAR(diff);
	diff |= regs->a6  ^ exp[3];
	OPTIMIZER_HIDE_VAR(diff);
	diff |= regs->a7  ^ exp[4];
	OPTIMIZER_HIDE_VAR(diff);
	diff |= regs->a8  ^ exp[5];
	OPTIMIZER_HIDE_VAR(diff);
	diff |= regs->a9  ^ exp[6];
	OPTIMIZER_HIDE_VAR(diff);
	diff |= regs->a10 ^ exp[7];
	OPTIMIZER_HIDE_VAR(diff);

	return diff;
}

static bool is_phys_range_allowed(const struct example_heap_config *config, u64 phys_start, u64 phys_end)
{
	int i;

	for (i = 0; i < config->nr_ranges; i++) {
		u64 cfg_start = config->ranges[i].start;
		u64 cfg_size = config->ranges[i].size;
		u64 cfg_end;

		if (check_add_overflow(cfg_start, cfg_size, &cfg_end))
			/* TODO probably should validate config ranges don't overflow in hyp_init */
			continue;

		if (phys_start >= cfg_start && phys_end <= cfg_end)
			return true;
	}

	return false;
}

static bool is_pfn_range_allowed(const struct example_heap_config *config,
				 u64 pfn, u64 nr_pages)
{
	u64 phys, size, end;

	if (check_shl_overflow(pfn, PAGE_SHIFT, &phys))
		return false;

	if (check_shl_overflow(nr_pages, PAGE_SHIFT, &size))
		return false;

	if (check_add_overflow(phys, size, &end))
		return false;

	return is_phys_range_allowed(config, phys, end);

}

void protect_page(struct user_pt_regs *regs)
{
	u64 pfn = regs->regs[1];
	u64 nr_pages = regs->regs[2];

	if (!is_pfn_range_allowed(vm_state.config, pfn, nr_pages)) {
		regs->regs[1] = -EPERM;
	} else {
		regs->regs[1] = ops->host_stage2_mod_prot(regs->regs[1], 0,
							  regs->regs[2], true);
	}

	regs->regs[0] = SMCCC_RET_SUCCESS;
}

void unprotect_page(struct user_pt_regs *regs)
{
	u64 pfn = regs->regs[1];
	u64 nr_pages = regs->regs[2];

	if (!is_pfn_range_allowed(vm_state.config, pfn, nr_pages)) {
		regs->regs[1] = -EPERM;
	} else {
		regs->regs[1] = ops->host_stage2_mod_prot(regs->regs[1],
							  KVM_PGTABLE_PROT_RWX,
							  regs->regs[2], true);
	}

	/* TODO in a real implementation memory should likely be cleared before returning it to the host. */

	regs->regs[0] = SMCCC_RET_SUCCESS;
}

/*  TODO: properly mark as 64 bit fast call */
#define SMC_ACCEPT_SECURE_BUF	0x123

static enum pkvm_smc_handler_ret smc_handler(struct arm_smccc_1_2_regs *regs,
					     struct arm_smccc_1_2_regs *res,
					     pkvm_handle_t handle)
{
	u64 ipa, nr_pages;
	int ret;
	u64* hyp_token = (u64*)vm_state.token_haddr;

	(ops->memcpy)(res, regs, sizeof(*res));

	if (regs->a0 != SMC_ACCEPT_SECURE_BUF)
		return GUEST_SMC_NOT_HANDLED;

	if (constant_time_token_neq(regs, hyp_token)) {
		res->a0 = -EACCES;
		return GUEST_SMC_HANDLED;
	}

	ipa = regs->a1;
	nr_pages = regs->a2;
	ret = ops->guest_accept_module_prot_page(ipa, nr_pages);
	if (ret == -ENOMEM)
		return GUEST_SMC_NEED_TOPUP;

	vm_state.bound_handle = handle;
	res->a0 = (u64)ret;

	return GUEST_SMC_HANDLED;
}

static int module_owned_fault_handler(u64 phys, u64 ipa, u64 size, pkvm_handle_t handle)
{
	u64 phys_end;

	/* pKVM will only call this fault handler if the provided handle and IPA have
	 * been marked as accepted in our smc_handler above. On VM destroy, module-owned
	 * page state is reset by pKVM. We can be sure that if we're handling a fault,
	 * curr_memshare_vm is pointing to the most recently authenticated VM and that
	 * even if the handle is re-used, we can't reach this point again without another
	 * successful smc_handler call to accept the given IPA range.  */
	if (!vm_state.bound_handle || vm_state.bound_handle != handle)
		return -EPERM;

	if (check_add_overflow(phys, size, &phys_end))
		return -EPERM;

	if (!is_phys_range_allowed(vm_state.config, phys, phys_end))
		return -EPERM;

	/* In this example there is only 1 guest, so we can assume that if we've validated
	 * the fault is within the configured heap's memory ranges that it is safe to share
	 * with the guest. If we were dealing with multiple guests, we'd need to be more
	 * sophisticated and either track page state per VM or ensure that allowlisted
	 * address ranges do not overlap between VMs.
	 */

	return 0;
}


int hyp_init(const struct pkvm_module_ops *__ops)
{
	int ret;

	ops = __ops;

	if (!ex_heap_config.token_paddr || !ex_heap_config.nr_ranges)
		return -EINVAL;

	vm_state.config = &ex_heap_config;

	ret = ops->create_private_mapping(vm_state.config->token_paddr,
					  MEMSHARE_TOKEN_SIZE,
					  PAGE_HYP_RO, &vm_state.token_haddr);
	if (ret)
		return ret;

	ret = ops->register_guest_smc_handler(smc_handler);
	if (ret)
		return ret;

	ret = ops->register_guest_accept_module_owned_handler(module_owned_fault_handler);
	if (ret)
		return ret;

	return 0;
}
