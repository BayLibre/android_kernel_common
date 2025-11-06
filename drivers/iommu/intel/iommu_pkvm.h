/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright © 2025, Google.
 */

#ifndef _INTEL_IOMMU_PKVM_H_
#define _INTEL_IOMMU_PKVM_H_

#include <asm/pkvm.h>

static inline long pkvm_hc_enable_iommu(unsigned long reg_phys,
		unsigned long root_gpa)
{
	long ret = 0;

	if (pkvm_pviommu_enabled())
		ret = kvm_hypercall2(PKVM_HC_ENABLE_IOMMU,
				reg_phys, root_gpa);

	return ret;
}

static inline long pkvm_hc_disable_iommu(unsigned long reg_phys)
{
	long ret = 0;

	if (pkvm_pviommu_enabled())
		ret = kvm_hypercall1(PKVM_HC_DISABLE_IOMMU, reg_phys);

	return ret;
}
#endif

