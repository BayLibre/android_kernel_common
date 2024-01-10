// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt) "smccc: KVM: " fmt

#include <linux/arm-smccc.h>
#include <linux/bitmap.h>
#include <linux/cache.h>
#include <linux/kernel.h>
#include <linux/memory.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/types.h>

#include <asm/hypervisor.h>

void __weak kvm_arm_init_hyp_services(void) {}

static DECLARE_BITMAP(__kvm_arm_hyp_services, ARM_SMCCC_KVM_NUM_FUNCS) __ro_after_init = { };

static unsigned long memshare_granule_sz;

void kvm_init_memrelinquish_services(void)
{
	int i;
	struct arm_smccc_res res;
	const u32 funcs[] = {
		ARM_SMCCC_KVM_FUNC_HYP_MEMINFO,
		ARM_SMCCC_KVM_FUNC_MEM_RELINQUISH,
	};

	for (i = 0; i < ARRAY_SIZE(funcs); ++i) {
		if (!kvm_arm_hyp_service_available(funcs[i]))
			return;
	}

	arm_smccc_1_1_invoke(ARM_SMCCC_VENDOR_HYP_KVM_HYP_MEMINFO_FUNC_ID,
			     0, 0, 0, &res);
	if (res.a0 > PAGE_SIZE) /* Includes error codes */
		return;

	memshare_granule_sz = res.a0;
}

bool kvm_has_memrelinquish_services(void)
{
	return !!memshare_granule_sz;
}
EXPORT_SYMBOL_GPL(kvm_has_memrelinquish_services);

#ifdef CONFIG_MEMORY_RELINQUISH
static void page_relinquish(struct page *page)
{
	phys_addr_t phys, end;
	u32 func_id = ARM_SMCCC_VENDOR_HYP_KVM_MEM_RELINQUISH_FUNC_ID;

	if (!memshare_granule_sz)
		return;

	phys = page_to_phys(page);
	end = phys + PAGE_SIZE;

	while (phys < end) {
		struct arm_smccc_res res;

		arm_smccc_1_1_invoke(func_id, phys, 0, 0, &res);
		BUG_ON(res.a0 != SMCCC_RET_SUCCESS);

		phys += memshare_granule_sz;
	}
}
#endif

void __init kvm_init_hyp_services(void)
{
	struct arm_smccc_res res;
	u32 val[4];

	if (arm_smccc_1_1_get_conduit() != SMCCC_CONDUIT_HVC)
		return;

	arm_smccc_1_1_invoke(ARM_SMCCC_VENDOR_HYP_CALL_UID_FUNC_ID, &res);
	if (res.a0 != ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_0 ||
	    res.a1 != ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_1 ||
	    res.a2 != ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_2 ||
	    res.a3 != ARM_SMCCC_VENDOR_HYP_UID_KVM_REG_3)
		return;

	memset(&res, 0, sizeof(res));
	arm_smccc_1_1_invoke(ARM_SMCCC_VENDOR_HYP_KVM_FEATURES_FUNC_ID, &res);

	val[0] = lower_32_bits(res.a0);
	val[1] = lower_32_bits(res.a1);
	val[2] = lower_32_bits(res.a2);
	val[3] = lower_32_bits(res.a3);

	bitmap_from_arr32(__kvm_arm_hyp_services, val, ARM_SMCCC_KVM_NUM_FUNCS);

	pr_info("hypervisor services detected (0x%08lx 0x%08lx 0x%08lx 0x%08lx)\n",
		 res.a3, res.a2, res.a1, res.a0);

	kvm_arm_init_hyp_services();

#ifdef CONFIG_MEMORY_RELINQUISH
	hyp_ops.page_relinquish = page_relinquish;
#endif
}

bool kvm_arm_hyp_service_available(u32 func_id)
{
	if (func_id >= ARM_SMCCC_KVM_NUM_FUNCS)
		return false;

	return test_bit(func_id, __kvm_arm_hyp_services);
}
EXPORT_SYMBOL_GPL(kvm_arm_hyp_service_available);
