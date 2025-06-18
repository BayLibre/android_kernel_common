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

struct pkvm_pgtable_cap guest_pgt_cap;
struct pkvm_pgtable_ops *guest_pgt_ops;

/*
 * FIXME: temporarily reusing the shadow pgt memory pool.
 * Replace it with a memcache supplied by KVM-high.
 */

static void *guest_mmu_zalloc_page(void)
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

int pkvm_vm_mmu_init(struct pkvm_vm *pkvm_vm)
{
	pkvm_vm->pgt_mm_ops = (struct pkvm_mm_ops) {
		.phys_to_virt = pkvm_phys_to_virt,
		.virt_to_phys = pkvm_virt_to_phys,
		.zalloc_page = guest_mmu_zalloc_page,
		.get_page = guest_mmu_get_page,
		.put_page = guest_mmu_put_page,
		.page_count = hyp_page_count,
		.flush_tlb = NULL,
		.flush_cache = NULL,
	};
	pkvm_vm->pgt_lock = __PKVM_SPINLOCK_UNLOCKED;

	return pkvm_pgtable_init(&pkvm_vm->pgt, &pkvm_vm->pgt_mm_ops, guest_pgt_ops,
				 &guest_pgt_cap, true);
}

static int guest_pgt_map_leaf(struct pkvm_pgtable *pgt, unsigned long vaddr, int level,
			      void *ptep, struct pgt_flush_data *flush_data, void *arg)
{
	struct pkvm_pgtable_map_data *data = arg;
	u64 size = pgt->pgt_ops->pgt_level_to_size(level);
	struct kvm *kvm = pgt_to_kvm(pgt);
	int ret;

	if (pgt->pgt_ops->pgt_entry_present(ptep)) {
		/*
		 * If the host wants to map the gpa to a different hpa,
		 * it should unmap it first.
		 */
		if (pgt->pgt_ops->pgt_entry_to_phys(ptep) != data->phys)
			return -EBUSY;

		/*
		 * pKVM does not support changing permissions of a mapped page yet,
		 * and even when it will, it will be done via a separate hypercall.
		 */
		if ((*(u64 *)ptep & guest_pgt_cap.prot_mask) !=
		    (data->prot & guest_pgt_cap.prot_mask))
			return -EBUSY;

		/*
		 * It is possible that another CPU has just created the same mapping
		 * when multiple CPUs touch the same page simultaneously.
		 * For simplicity check this on pKVM side, since the host doesn't
		 * track guest mappings in any data structure yet.
		 */
		return -EEXIST;
	}

	/*
	 * TODO: use a more suitable API than the existing page state API. Why walk
	 * the page table once again to reach this PTE if we are already at it?
	 * We could combine these 2 layers (MMU and page state API) into one layer.
	 */
	if (pkvm_is_protected_vm(kvm))
		ret = __pkvm_host_donate_guest(data->phys, pgt, vaddr, size, data->prot);
	else
		ret = __pkvm_host_share_guest(data->phys, pgt, vaddr, size, data->prot);

	return ret;
}

int pkvm_vm_mmu_map(int vm_handle, u64 gpa, u64 hpa, u64 size, bool writable)
{
	struct pkvm_vm *pkvm_vm;
	u64 prot;
	int ret;

	pkvm_vm = get_pkvm_vm(vm_handle);
	if (!pkvm_vm)
		return -EINVAL;

	if (!writable && pkvm_is_protected_vm(to_kvm(pkvm_vm))) {
		ret = -EPERM;
		goto put_pkvm_vm;
	}

	prot = writable ? guest_pgt_cap.prot_rwx :
			  guest_pgt_cap.prot_rx;
	prot |= guest_pgt_cap.mt_memory;

	pkvm_spin_lock(&pkvm_vm->pgt_lock);
	ret = pkvm_pgtable_map(&pkvm_vm->pgt, gpa, hpa, size, 0, prot, guest_pgt_map_leaf);
	pkvm_spin_unlock(&pkvm_vm->pgt_lock);

put_pkvm_vm:
	put_pkvm_vm(pkvm_vm);
	return ret;
}

void pkvm_vm_mmu_destroy(struct pkvm_vm *pkvm_vm)
{
	pkvm_pgtable_destroy(&pkvm_vm->pgt, NULL);
}
