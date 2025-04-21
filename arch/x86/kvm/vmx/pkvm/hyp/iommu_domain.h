/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2022 Intel Corporation. */

#ifndef _PKVM_IOMMU_DOMAIN_H_
#define _PKVM_IOMMU_DOMAIN_H_

#include "pkvm_hyp.h"

/*
 * Represents a host iommu_domain/dmar_domain
 * Main function is to manage IO page tables.
 */
struct pkvm_iommu_domain {
	atomic_t refcount;
	unsigned long index;
	bool iommu_coherency;
	u64 pgd;
	int iommu_superpage;
	int gaw;
	int agaw;
	/*
	 * Lock to protect the mapping operations
	 * on this domain.
	 */
	pkvm_spinlock_t lock;

	struct hlist_node hnode;
};

#define DEFAULT_DOMAIN_ADDRESS_WIDTH 57

#define __DOMAIN_MAX_PFN(gaw)  ((((uint64_t)1) << ((gaw) - VTD_PAGE_SHIFT)) - 1)
#define __DOMAIN_MAX_ADDR(gaw) ((((uint64_t)1) << (gaw)) - 1)

/* We limit DOMAIN_MAX_PFN to fit in an unsigned long, and DOMAIN_MAX_ADDR
   to match. That way, we can use 'unsigned long' for PFNs with impunity. */
#define DOMAIN_MAX_PFN(gaw)	((unsigned long) min_t(uint64_t, \
				__DOMAIN_MAX_PFN(gaw), (unsigned long)-1))
#define DOMAIN_MAX_ADDR(gaw)	(((uint64_t)__DOMAIN_MAX_PFN(gaw)) << VTD_PAGE_SHIFT)

struct pkvm_iommu_domain *pkvm_alloc_iommu_domain(u64 pgd);
struct pkvm_iommu_domain *pkvm_get_iommu_domain(u64 pgd);
void pkvm_put_iommu_domain(struct pkvm_iommu_domain *iommu_domain);

unsigned long pkvm_iommu_domain_map(struct kvm_vcpu *hvcpu, unsigned long param_gva);
unsigned long pkvm_iommu_domain_unmap(struct kvm_vcpu *hvcpu, unsigned long param_gva);
unsigned long pkvm_iommu_domain_iova_to_phys(struct kvm_vcpu *hvcpu, unsigned long param_gva);
#endif
