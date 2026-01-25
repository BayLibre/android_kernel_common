// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2026 Google.
 *
 */
#include <asm/kvm_pkvm.h>
#include "pkvm/mmu.h"
#include "pkvm/memory.h"
#include "pkvm/pkvm.h"
#include "pkvm/debug.h"
#include "../iommu.h"

int pkvm_iommu_qi_submit(u64 phys, u64 desc_gpa, u32 count, u32 options)
{
	struct intel_iommu *iommu = iommu_from_phys(phys);

	if (!iommu)
		return -EINVAL;

	BUG_ON(!iommu->qi);

	return qi_submit_sync(iommu, pkvm_host_gpa_to_virt(desc_gpa),
			      count, options);
}
