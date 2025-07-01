/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PKVM_X86_MMU_H
#define __PKVM_X86_MMU_H

#include "pkvm.h"
//FIXME: clean up the header files
#include <vmx/pkvm/hyp/pgtable.h>
#include <pkvm.h>

extern struct pkvm_pgtable_cap guest_pgt_cap;
extern struct pkvm_pgtable_ops *guest_pgt_ops;

int pkvm_vm_mmu_init(struct pkvm_vm *pkvm_vm);
void pkvm_vm_mmu_destroy(struct pkvm_vm *pkvm_vm);
int pkvm_vm_mmu_map(int vm_handle, int vcpu_handle, u64 gpa, u64 hpa, u64 size, bool writable);
int pkvm_vm_mmu_unmap(int handle, u64 gpa, u64 size);
int pkvm_vm_mmu_age(int handle, u64 gpa, u64 size, bool mkold);

#endif /* __PKVM_X86_MMU_H */
