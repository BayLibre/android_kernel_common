// SPDX-License-Identifier: GPL-2.0-only
#ifndef __GIC_V3_ITS_PKVM_MODULES__
#define __GIC_V3_ITS_PKVM_MODULES__

#if defined(__KVM_NVHE_HYPERVISOR__) && defined(MODULE)

#include <asm/kvm_pkvm_module.h>

extern const struct pkvm_module_ops *mod_ops;

#define CALL_FROM_OPS(fn, ...) ((mod_ops)->(fn)(__VA_ARGS__))

#define register_host_perm_fault_handler(fn) \
	CALL_FROM_OPS(register_host_perm_fault_handler, fn)

#define host_stage2_mod_prot(pfn, prot, nr_pages, update_iommu) \
	CALL_FROM_OPS(host_stage2_mod_prot, pfn, prot, nr_pages, update_iommu)

#define host_donate_hyp(pfn, nr_pages) \
	CALL_FROM_OPS(host_donate_hyp, pfn, nr_pages)

#define create_private_mapping(phys, size, prot, haddr) \
	CALL_FROM_OPS(create_private_mapping, phys, size, prot, haddr)

#define host_share_hyp(pfn) CALL_FROM_OPS(host_share_hyp, pfn)

#define host_unshare_hyp(pfn) CALL_FROM_OPS(host_unshare_hyp, pfn)

#define host_stage2_get_leaf(phys, ptep, level) \
	CALL_FROM_OPS(host_stage2_get_leaf, phys, ptep, level)

#endif /* defined(__KVM_NVHE_HYPERVISOR__) && defined(MODULE) */

#endif /* __GIC_V3_ITS_PKVM_MODULES__ */
