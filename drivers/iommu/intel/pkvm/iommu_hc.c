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

	/*
	* Note: We do not need to host_share_hyp desc_gpa memory before
	* doing qi_submit_sync. This hypercall is temporary and will be
	* removed in future patches. It will be replaced by dedicated
	* hypercall specifically for submitting QI_IEC_TYPE.
	*/
	return qi_submit_sync(iommu, pkvm_host_gpa_to_virt(desc_gpa),
			      count, options);
}
