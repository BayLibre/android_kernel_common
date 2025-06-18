// SPDX-License-Identifier: GPL-2.0
#include <asm/kvm_pkvm.h>
#include "pkvm.h"
#include "mmu.h"
//FIXME: clean up the header files
#include <vmx/pkvm/hyp/pkvm_hyp.h>
#include <vmx/pkvm/hyp/pgtable.h>
#include <vmx/pkvm/hyp/ept.h>
#include <vmx/pkvm/hyp/mem_protect.h>
#include <vmx/pkvm/hyp/memory.h>

int pkvm_vm_mmu_init(struct pkvm_vm *pkvm_vm)
{
	/*
	 * FIXME: temporarily reusing the shadow EPT memory pool.
	 * Replace it with a memcache supplied by KVM-high.
	 *
	 * FIXME: temporarily reusing shadow_ept_mm_ops. In particular,
	 * it deadlocks with shadow_ept_flush_tlb()!
	 *
	 * FIXME: temporarily using ept_ops and ept_cap directly.
	 * EPT is VMX-specific, so it should be in vendor code.
	 */
	return pkvm_pgtable_init(&pkvm_vm->pgt, &shadow_ept_mm_ops, &ept_ops,
				 &pkvm_hyp->ept_cap, true);
}

int pkvm_vm_mmu_map(int handle, u64 gpa, u64 hpa, u64 size)
{
	u64 prot = HOST_EPT_DEF_MEM_PROT;	/* FIXME */
	struct pkvm_vm *pkvm_vm;
	int ret;

	pkvm_vm = get_pkvm_vm(handle);
	if (!pkvm_vm)
		return -EINVAL;

	pkvm_spin_lock(&pkvm_vm->lock);

	if (pkvm_is_protected_vm(to_kvm(pkvm_vm)))
		ret = __pkvm_host_donate_guest(hpa, &pkvm_vm->pgt, gpa, size, prot);
	else
		ret = __pkvm_host_share_guest(hpa, &pkvm_vm->pgt, gpa, size, prot);

	pkvm_spin_unlock(&pkvm_vm->lock);

	put_pkvm_vm(pkvm_vm);
	return ret;
}

static int pkvm_vm_pgt_free_leaf(struct pkvm_pgtable *pgt, unsigned long vaddr, int level,
				 void *ptep, struct pgt_flush_data *flush_data, void *arg)
{
	unsigned long phys = pgt->pgt_ops->pgt_entry_to_phys(ptep);
	unsigned long size = pgt->pgt_ops->pgt_level_to_size(level);
	struct pkvm_vm *pkvm_vm = container_of(pgt, struct pkvm_vm, pgt);

	/* TODO: this actually happens, why? */
	if (WARN_ON_ONCE(!pgt->pgt_ops->pgt_entry_present(ptep)))
		return 0;

	/*
	 * The pgtable_free_cb in this current page walker is still walking
	 * the page state table so cannot allow the  __pkvm_host_unshare_guest()
	 * or __pkvm_host_undonate_guest() release the page table pages. So
	 * we shall get_page before these APIs called, then put_page to allow
	 * pgtable_free_cb free table pages with correct refcount.
	 */
	if (pkvm_is_protected_vm(to_kvm(pkvm_vm))) {
		void *virt = pgt->mm_ops->phys_to_virt(phys);

		/*
		 * Before returning to host, the memory page previously owned by
		 * protected VM shall be memset to 0 to avoid secret leakage.
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
	pkvm_pgtable_destroy(&pkvm_vm->pgt, pkvm_vm_pgt_free_leaf);
}
