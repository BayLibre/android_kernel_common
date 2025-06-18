/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PKVM_X86_MMU_H
#define __PKVM_X86_MMU_H

int pkvm_vm_mmu_init(struct pkvm_vm *pkvm_vm);
void pkvm_vm_mmu_destroy(struct pkvm_vm *pkvm_vm);
int pkvm_vm_mmu_map(int handle, u64 gpa, u64 hpa, u64 size);

#endif /* __PKVM_X86_MMU_H */
