// SPDX-License-Identifier: GPL-2.0
#include <linux/kbuild.h>
#include <vmx/vmx.h>
#include "memory.h"
#include "pkvm.h"

#undef pr_fmt
#include "../../../../fs/pstore/ram_core.c"

int main(void)
{
	DEFINE(PKVM_VMEMMAP_ENTRY_SIZE, sizeof(struct pkvm_page));
#ifdef CONFIG_PKVM_INTEL
	DEFINE(PKVM_VMX_VM_SIZE, PKVM_VM_BASE_SIZE + sizeof(struct kvm_vmx));
	DEFINE(PKVM_VMX_VCPU_SIZE, PKVM_VCPU_BASE_SIZE + sizeof(struct vcpu_vmx));
#endif

	BLANK();
	DEFINE(PKVM_RAMOOPS_SIG_VAL, PERSISTENT_RAM_SIG);
	DEFINE(PKVM_RAMOOPS_BUFFER_SIG_OFFSET, offsetof(struct persistent_ram_buffer, sig));
	DEFINE(PKVM_RAMOOPS_BUFFER_START_OFFSET, offsetof(struct persistent_ram_buffer, start));
	DEFINE(PKVM_RAMOOPS_BUFFER_SIZE_OFFSET, offsetof(struct persistent_ram_buffer, size));
	DEFINE(PKVM_RAMOOPS_BUFFER_DATA_OFFSET, offsetof(struct persistent_ram_buffer, data));

	return 0;
}
