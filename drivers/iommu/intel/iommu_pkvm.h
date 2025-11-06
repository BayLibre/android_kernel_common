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

	return pkvm_hypercall(iommu_enable, reg_phys, root_gpa);

}

static inline long pkvm_hc_disable_iommu(unsigned long reg_phys)
{
	return pkvm_hypercall(iommu_disable, reg_phys);

}
#endif
