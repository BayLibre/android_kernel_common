/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright © 2025, Google.
 */

#ifndef _INTEL_IOMMU_PKVM_H_
#define _INTEL_IOMMU_PKVM_H_

#include <asm/kvm_pkvm.h>

static inline int pkvm_hc_qi_submit_sync(unsigned long reg_phys, unsigned long desc,
		unsigned int count)
{
	return pkvm_hypercall(iommu_submit_qi, reg_phys, desc, count);
}
#endif
