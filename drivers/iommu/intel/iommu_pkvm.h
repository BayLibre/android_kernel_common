/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright © 2025, Google.
 */

#ifndef _INTEL_IOMMU_PKVM_H_
#define _INTEL_IOMMU_PKVM_H_

#include <asm/kvm_pkvm.h>

#define pkvm_iommu_hypercall(hc, param_name, param)			\
	({									\
		struct pkvm_##param_name *p = get_this_pv_param(param_name);	\
		int ret;							\
		*p = *(param);							\
		ret = kvm_hypercall1((hc), (unsigned long)p);		\
		*(param) = *p;							\
		put_this_pv_param(p);						\
		ret;								\
	})

static inline long pkvm_hc_enable_iommu(unsigned long reg_phys,
		unsigned long root_gpa)
{

	return kvm_hypercall2(PKVM_HC_ENABLE_IOMMU, reg_phys, root_gpa);

}

static inline long pkvm_hc_disable_iommu(unsigned long reg_phys)
{
	return kvm_hypercall1(PKVM_HC_DISABLE_IOMMU, reg_phys);

}

static inline long pkvm_hc_iommu_clear_ce(struct pkvm_clear_translation_param *param)
{
	return pkvm_iommu_hypercall(PKVM_HC_IOMMU_CLEAR_CE, clear_translation_param, param);
}

static inline long pkvm_hc_iommu_set_lm_ce(struct pkvm_lm_context_param *param)
{
	return pkvm_iommu_hypercall(PKVM_HC_IOMMU_SET_LM_CE, lm_context_param, param);
}

static inline long pkvm_hc_iommu_set_sm_ce(struct pkvm_sm_context_param *param)
{
	return pkvm_iommu_hypercall(PKVM_HC_IOMMU_SET_SM_CE, sm_context_param, param);
}
#endif
