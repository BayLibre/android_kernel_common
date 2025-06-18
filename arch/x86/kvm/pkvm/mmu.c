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
#include <pkvm.h>

struct pkvm_pgtable_ops guest_mmu_ops;
struct pkvm_pgtable_cap guest_mmu_cap;

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

	return pkvm_pgtable_init(&pkvm_vm->pgt, &pkvm_vm->pgt_mm_ops, &guest_mmu_ops,
				 &guest_mmu_cap, true);
}

void pkvm_vm_mmu_destroy(struct pkvm_vm *pkvm_vm)
{
	pkvm_pgtable_destroy(&pkvm_vm->pgt, NULL);
}
