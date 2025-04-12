// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google
 */
#include <../drivers/iommu/intel/iommu.h>
#include <asm/pkvm_spinlock.h>
#include <pkvm.h>
#include "pkvm_hyp.h"
#include "gfp.h"
#include "memory.h"
#include "mmu.h"
#include "ept.h"
#include "pgtable.h"
#include "iommu_internal.h"
#include "debug.h"
#include "iommu_spgt.h"
#include "bug.h"
#include "iommu.h"

int initialize_iommu_pgt(struct pkvm_iommu *iommu)
{
	struct pkvm_pgtable *pgt = &iommu->pgt;
	struct viommu_reg *vreg = &iommu->viommu.vreg;

	if (!vreg->rta) {
		pkvm_err("pkvm: %s: iommu%d: host RTADDR_REG not set",
				__func__, iommu->iommu.seq_id);
		return -EINVAL;
	}

	pgt->root_pa = vreg->rta & VTD_PAGE_MASK;

	return 0;
}
