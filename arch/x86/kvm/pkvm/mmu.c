// SPDX-License-Identifier: GPL-2.0
#include <asm/kvm_pkvm.h>
#include <asm/pkvm_spinlock.h>
#include "pkvm.h"
#include "mmu.h"
//FIXME: clean up the header files
#include <vmx/pkvm/hyp/pkvm_hyp.h>
#include <vmx/pkvm/hyp/pgtable.h>
#include <vmx/pkvm/hyp/gfp.h>
#include <vmx/pkvm/hyp/ept.h>	//FIXME
#include <vmx/pkvm/hyp/mem_protect.h>
#include <vmx/pkvm/hyp/memory.h>
#include <pkvm.h>

/*
 * FIXME: temporarily reusing the shadow pgt memory pool.
 * Replace it with a memcache supplied by KVM-high.
 */

static void *guest_mmu_zalloc_page(void *mc)
{
	return hyp_alloc_pages(&shadow_pgt_pool, 0);
}

static void guest_mmu_get_page(void *vaddr)
{
	hyp_get_page(&shadow_pgt_pool, vaddr);
}

static void guest_mmu_put_page(void *vaddr)
{
	hyp_put_page(&shadow_pgt_pool, vaddr);
}

static void guest_tlb_shootdown(struct pkvm_pgtable *pgt,
				unsigned long addr,
				unsigned long size)
{
	struct pkvm_vm *pkvm_vm = pgt_to_pkvm(pgt);
	int i;

	pkvm_spin_lock(&pkvm_vm->lock);

	for (i = 0; i < to_kvm(pkvm_vm)->created_vcpus; i++) {
		struct pkvm_vcpu *pkvm_vcpu;
		struct kvm_vcpu *vcpu;

		pkvm_vcpu = pkvm_vm->vcpus[i];
		if (WARN_ON_ONCE(!pkvm_vcpu))
			continue;

		vcpu = to_kvm_vcpu(pkvm_vcpu);

		kvm_make_request(KVM_REQ_TLB_FLUSH_CURRENT, vcpu);
		pkvm_kick_vcpu(vcpu);
	}

	pkvm_spin_unlock(&pkvm_vm->lock);
}

int pkvm_vm_mmu_init(struct pkvm_vm *pkvm_vm)
{
	struct pkvm_pgtable_cap cap;
	struct pkvm_pgtable_ops *pgt_ops;

	pkvm_x86_call(get_mmu_caps)(&cap, &pgt_ops);

	pkvm_vm->pgt_mm_ops = (struct pkvm_mm_ops) {
		.phys_to_virt = pkvm_phys_to_virt,
		.virt_to_phys = pkvm_virt_to_phys,
		.zalloc_page = guest_mmu_zalloc_page,
		.get_page = guest_mmu_get_page,
		.put_page = guest_mmu_put_page,
		.page_count = hyp_page_count,
		.flush_tlb = guest_tlb_shootdown,
		.flush_cache = NULL,
	};
	pkvm_vm->pgt_lock = __PKVM_SPINLOCK_UNLOCKED;

	return pkvm_pgtable_init(&pkvm_vm->pgt, &pkvm_vm->pgt_mm_ops, pgt_ops,
				 &cap, true);
}

static bool range_has_pvmfw(struct kvm *kvm, u64 gpa_start, u64 gpa_end)
{
	struct kvm_protected_vm *pkvm = &kvm->arch.pkvm;
	u64 pvmfw_load_end = pkvm->pvmfw_load_addr + pvmfw_size;

	if (!pvmfw_present)
		return false;

	if (pkvm->pvmfw_load_addr == INVALID_GPA)
		return false;

	return gpa_end > pkvm->pvmfw_load_addr && gpa_start < pvmfw_load_end;
}

static int load_pvmfw_pages(struct kvm *kvm, u64 gpa, u64 phys, u64 size)
{
	u64 offset = gpa - kvm->arch.pkvm.pvmfw_load_addr;

	if (offset >= pvmfw_size)
		return -EINVAL;

	size = min(size, pvmfw_size - offset);
	if (!PAGE_ALIGNED(size) || !PAGE_ALIGNED(offset))
		return -EINVAL;

	memcpy(__pkvm_va(phys), __pkvm_va(pvmfw_base + offset), size);
	return 0;
}

static int guest_pgt_map_leaf(struct pkvm_pgtable *pgt, unsigned long vaddr, int level,
			      void *ptep, struct pgt_flush_data *flush_data, void *arg)
{
	struct pkvm_pgtable_map_data *data = arg;
	u64 size = pgt->pgt_ops->pgt_level_to_size(level);
	struct kvm *kvm = pgt_to_kvm(pgt);
	int ret;

	/*
	 * It is possible that another CPU has just created the same mapping
	 * when multiple CPUs touch the same page simultaneously.
	 * TODO: check this on the host side, before making the hypercall.
	 */
	if (pgt->pgt_ops->pgt_entry_present(ptep))
		return 0;

	/*
	 * TODO: use a more suitable API than the existing page state API. Why walk
	 * the page table once again to reach this PTE if we are already at it?
	 * We could combine these 2 layers (MMU and page state API) into a single layer.
	 */
	if (pkvm_is_protected_vm(kvm)) {
		ret = __pkvm_host_donate_guest(data->phys, pgt, vaddr, size, data->prot);
		if (ret)
			return ret;

		if (range_has_pvmfw(kvm, vaddr, vaddr + size)) {
			ret = load_pvmfw_pages(kvm, vaddr, data->phys, size);
			WARN_ON_ONCE(ret);
		}
	} else {
		ret = __pkvm_host_share_guest(data->phys, pgt, vaddr, size, data->prot);
	}

	return ret;
}

int pkvm_vm_mmu_map(int vm_handle, u64 gpa, u64 hpa, u64 size)
{
	u64 prot = HOST_EPT_DEF_MEM_PROT;	/* FIXME */
	struct pkvm_vm *pkvm_vm;
	int ret;

	pkvm_vm = get_pkvm_vm(vm_handle);
	if (!pkvm_vm)
		return -EINVAL;

	pkvm_spin_lock(&pkvm_vm->pgt_lock);
	ret = pkvm_pgtable_map(&pkvm_vm->pgt, gpa, hpa, size, 0, prot, guest_pgt_map_leaf, NULL);
	pkvm_spin_unlock(&pkvm_vm->pgt_lock);

	put_pkvm_vm(pkvm_vm);
	return ret;
}

static int guest_pgt_unmap_leaf(struct pkvm_pgtable *pgt, unsigned long vaddr, int level,
				void *ptep, struct pgt_flush_data *flush_data, void *arg)
{
	unsigned long phys = pgt->pgt_ops->pgt_entry_to_phys(ptep);
	unsigned long size = pgt->pgt_ops->pgt_level_to_size(level);
	int ret;

	if (WARN_ON_ONCE(!pgt->pgt_ops->pgt_entry_present(ptep)))
		return 0;

	pgt->mm_ops->get_page(ptep);
	ret = __pkvm_host_unshare_guest(phys, pgt, vaddr, size);
	pgt->mm_ops->put_page(ptep);

	flush_data->flushtlb = true;

	return ret;
}

int pkvm_vm_mmu_unmap(int vm_handle, u64 gpa, u64 size)
{
	struct pkvm_vm *pkvm_vm;
	int ret;

	pkvm_vm = get_pkvm_vm(vm_handle);
	if (!pkvm_vm)
		return -EINVAL;

	if (pkvm_is_protected_vm(to_kvm(pkvm_vm))) {
		ret = -EPERM;
		goto put_pkvm_vm;
	}

	pkvm_spin_lock(&pkvm_vm->pgt_lock);
	ret = pkvm_pgtable_unmap(&pkvm_vm->pgt, gpa, size, guest_pgt_unmap_leaf);
	pkvm_spin_unlock(&pkvm_vm->pgt_lock);

put_pkvm_vm:
	put_pkvm_vm(pkvm_vm);
	return ret;
}

static int guest_pgt_free_leaf(struct pkvm_pgtable *pgt, unsigned long vaddr, int level,
			       void *ptep, struct pgt_flush_data *flush_data, void *arg)
{
	unsigned long phys = pgt->pgt_ops->pgt_entry_to_phys(ptep);
	unsigned long size = pgt->pgt_ops->pgt_level_to_size(level);
	struct kvm *kvm = pgt_to_kvm(pgt);

	if (!pgt->pgt_ops->pgt_entry_mapped(ptep))
		return 0;

	/* Guest may only share its pages, not donate them. */
	WARN_ON_ONCE(!pgt->pgt_ops->pgt_entry_present(ptep));

	/*
	 * The pgtable_free_cb in this current page walker is still walking
	 * the page table so we cannot allow __pkvm_host_unshare_guest()
	 * or __pkvm_host_undonate_guest() to release the page table pages.
	 * So we shall get_page before calling these APIs, then put_page
	 * to let pgtable_free_cb free table pages with correct refcount.
	 */
	if (pkvm_is_protected_vm(kvm)) {
		void *virt = pgt->mm_ops->phys_to_virt(phys);

		/*
		 * Wipe the protected VM memory page before giving it back
		 * to host, to avoid secrets leakage.
		 */
		memset(virt, 0, size);
		pkvm_clflush_cache_range(virt, size);

		pgt->mm_ops->get_page(ptep);
		WARN_ON_ONCE(__pkvm_host_undonate_guest(phys, pgt, vaddr, size));
		pgt->mm_ops->put_page(ptep);
	} else {
		pgt->mm_ops->get_page(ptep);
		WARN_ON_ONCE(__pkvm_host_unshare_guest(phys, pgt, vaddr, size));
		pgt->mm_ops->put_page(ptep);
	}

	return 0;
}

void pkvm_vm_mmu_destroy(struct pkvm_vm *pkvm_vm)
{
	/* vCPUs are already torn down, no need to flush TLBs. */
	pkvm_vm->pgt.mm_ops->flush_tlb = NULL;

	pkvm_pgtable_destroy(&pkvm_vm->pgt, guest_pgt_free_leaf);
}
