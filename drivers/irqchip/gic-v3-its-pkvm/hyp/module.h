// SPDX-License-Identifier: GPL-2.0-only
#ifndef __GIC_V3_ITS_PKVM_MODULES__
#define __GIC_V3_ITS_PKVM_MODULES__

#if defined(__KVM_NVHE_HYPERVISOR__) && defined(MODULE)

#include <asm/kvm_pkvm_module.h>
#include <asm/kvm_mmu.h>

extern const struct pkvm_module_ops *mod_ops;

#define CALL_FROM_OPS(fn, ...) ((mod_ops)->fn(__VA_ARGS__))

#undef memcpy

#define register_host_perm_fault_handler(fn) \
	CALL_FROM_OPS(register_host_perm_fault_handler, fn)

#define host_stage2_mod_prot(pfn, prot, nr_pages, update_iommu) \
	CALL_FROM_OPS(host_stage2_mod_prot, pfn, prot, nr_pages, update_iommu)

#define host_donate_hyp(pfn, nr_pages) \
	CALL_FROM_OPS(host_donate_hyp, pfn, nr_pages)

#define hyp_donate_host(pfn, nr_pages) \
	CALL_FROM_OPS(hyp_donate_host, pfn, nr_pages)

#define create_private_mapping(phys, size, prot, haddr) \
	CALL_FROM_OPS(create_private_mapping, phys, size, prot, haddr)

#define host_share_hyp(pfn) CALL_FROM_OPS(host_share_hyp, pfn)

#define host_unshare_hyp(pfn) CALL_FROM_OPS(host_unshare_hyp, pfn)

#define host_stage2_get_leaf(phys, ptep, level) \
	CALL_FROM_OPS(host_stage2_get_leaf, phys, ptep, level)

#define hyp_phys_to_virt(x) __hyp_va(x)

#define memcpy(to, from, count) CALL_FROM_OPS(memcpy, to, from, count)

#define hyp_pin_shared_mem(x, y) CALL_FROM_OPS(pin_shared_mem, x, y)

#define hyp_unpin_shared_mem(x, y) CALL_FROM_OPS(unpin_shared_mem, x, y)

#define hyp_alloc(sz) CALL_FROM_OPS(hyp_alloc, sz)

#define hyp_free(p) CALL_FROM_OPS(hyp_free, p)

#define hyp_puts(s) CALL_FROM_OPS(puts, s)

#define hyp_putx64(n) CALL_FROM_OPS(putx64, n)

#endif /* defined(__KVM_NVHE_HYPERVISOR__) && defined(MODULE) */

#endif /* __GIC_V3_ITS_PKVM_MODULES__ */
