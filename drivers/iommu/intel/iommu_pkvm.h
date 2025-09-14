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
		ret = pkvm_hypercall(hc, (unsigned long)p);		\
		*(param) = *p;							\
		put_this_pv_param(p);						\
		ret;								\
	})

static inline long pkvm_hc_enable_iommu(unsigned long reg_phys,
		unsigned long root_gpa)
{

	return pkvm_hypercall(iommu_enable, reg_phys, root_gpa);

}

static inline long pkvm_hc_disable_iommu(unsigned long reg_phys)
{
	return pkvm_hypercall(iommu_disable, reg_phys);

}

static inline long pkvm_hc_iommu_clear_ce(struct pkvm_clear_translation_param *param)
{
	return pkvm_iommu_hypercall(iommu_clear_ce, clear_translation_param, param);
}

static inline long pkvm_hc_iommu_set_lm_ce(struct pkvm_lm_context_param *param)
{
	return pkvm_iommu_hypercall(iommu_set_lm_ce, lm_context_param, param);
}

static inline long pkvm_hc_iommu_set_sm_ce(struct pkvm_sm_context_param *param)
{
	return pkvm_iommu_hypercall(iommu_set_sm_ce, sm_context_param, param);
}

static inline long pkvm_hc_iommu_clear_pasid_entry(struct pkvm_clear_translation_param *param)
{
	return pkvm_iommu_hypercall(iommu_clear_pasid_entry,
			clear_translation_param, param);
}

static inline long pkvm_hc_iommu_set_pasid_fl(struct pkvm_pasid_table_param *param)
{
	return pkvm_iommu_hypercall(iommu_set_pasid_fl,
			pasid_table_param, param);
}

static inline long pkvm_hc_iommu_set_pasid_sl(struct pkvm_pasid_table_param *param)
{
	return pkvm_iommu_hypercall(iommu_set_pasid_sl,
			pasid_table_param, param);
}
#endif
