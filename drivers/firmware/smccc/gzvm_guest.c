#define pr_fmt(fmt) "smccc: GZVM: " fmt

#include <linux/arm-smccc.h>
#include <linux/bitmap.h>
#include <linux/cache.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include <asm/hypervisor.h>

void gzvm_arm_init_hyp_services(void)
{
	gzvm_init_ioremap_services();
	gzvm_init_memshare_services();
	gzvm_init_memrelinquish_services();
}

void __init gzvm_init_hyp_services(void)
{
	struct arm_smccc_res res;

	if (arm_smccc_1_1_get_conduit() != SMCCC_CONDUIT_HVC)
		return;

	memset(&res, 0, sizeof(res));
	arm_smccc_1_1_invoke(ARM_SMCCC_VENDOR_HYP_CALL_UID_FUNC_ID, &res);
	if (res.a0 != ARM_SMCCC_VENDOR_HYP_UID_GZVM_REG_0 ||
	    res.a1 != ARM_SMCCC_VENDOR_HYP_UID_GZVM_REG_1 ||
	    res.a2 != ARM_SMCCC_VENDOR_HYP_UID_GZVM_REG_2 ||
	    res.a3 != ARM_SMCCC_VENDOR_HYP_UID_GZVM_REG_3)
		return;

	gzvm_arm_init_hyp_services();
}
