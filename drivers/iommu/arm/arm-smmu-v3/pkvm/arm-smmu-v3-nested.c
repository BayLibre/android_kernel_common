// SPDX-License-Identifier: GPL-2.0
/*
 * pKVM hyp driver for the Arm SMMUv3
 *
 * Copyright (C) 2022 Linaro Ltd.
 */
#include <asm/kvm_hyp.h>

#include <nvhe/iommu.h>

#include "arm_smmu_v3_nested.h"

size_t __ro_after_init kvm_hyp_arm_smmu_v3_count;
struct hyp_arm_smmu_v3_device *kvm_hyp_arm_smmu_v3_smmus;

static int smmu_init(void)
{
	return -ENOSYS;
}

static void smmu_host_stage2_idmap(struct kvm_hyp_iommu_domain *domain, phys_addr_t start, phys_addr_t end, int prot)
{

}

static int smmu_alloc_domain(struct kvm_hyp_iommu_domain *domain, int type)
{
	return 0;
}

static void smmu_free_domain(struct kvm_hyp_iommu_domain *domain)
{
}

static struct kvm_hyp_iommu *smmu_id_to_iommu(pkvm_handle_t smmu_id)
{
	return 0;
}

/* Shared with the kernel driver in EL1 */
struct kvm_iommu_ops smmu_ops = {
	.init				= smmu_init,
	.host_stage2_idmap		= smmu_host_stage2_idmap,
	.alloc_domain			= smmu_alloc_domain,
	.free_domain			= smmu_free_domain,
	.get_iommu_by_id		= smmu_id_to_iommu,
};
