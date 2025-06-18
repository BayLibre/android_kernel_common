// SPDX-License-Identifier: GPL-2.0
#include <asm/kvm_pkvm.h>
#include "pkvm.h"
#include "mmu.h"
//FIXME: clean up the header files
#include <vmx/pkvm/hyp/pkvm_hyp.h>
#include <vmx/pkvm/hyp/pgtable.h>
#include <vmx/pkvm/hyp/ept.h>

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

void pkvm_vm_mmu_destroy(struct pkvm_vm *pkvm_vm)
{
	pkvm_pgtable_destroy(&pkvm_vm->pgt, NULL);
}
