// SPDX-License-Identifier: GPL-2.0
/*
 * CR4 and CPUID sync test (pKVM variant)
 */

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "pkvm/pkvm_util.h"

static void guest_code(void)
{
	u32 regs[4] = {
		[KVM_CPUID_EAX] = X86_FEATURE_OSXSAVE.function,
		[KVM_CPUID_ECX] = X86_FEATURE_OSXSAVE.index,
	};
	u64 old_cr4 = get_cr4();
	u64 cleared_cr4 = old_cr4 & ~X86_CR4_OSXSAVE;

	/* CR4.OSXSAVE should be enabled by default (for selftests vCPUs). */
	GUEST_ASSERT(old_cr4 & X86_CR4_OSXSAVE);

	/* Verify CR4.OSXSAVE == CPUID.OSXSAVE. */
	GUEST_ASSERT(this_cpu_has(X86_FEATURE_OSXSAVE));

	/*
	 * In pKVM, host cannot directly modify guest CR4.  Toggle CR4.OSXSAVE
	 * in guest, run CPUID while disabled, then restore CR4.
	 */
	asm volatile (
		"mov %[cleared_cr4], %%cr4\n\t"
		"cpuid\n\t"
		"mov %[old_cr4], %%cr4\n\t"
		: "+a" (regs[KVM_CPUID_EAX]),
		  "=b" (regs[KVM_CPUID_EBX]),
		  "+c" (regs[KVM_CPUID_ECX]),
		  "=d" (regs[KVM_CPUID_EDX])
		: [old_cr4] "r" (old_cr4),
		  [cleared_cr4] "r" (cleared_cr4)
		: "memory"
	);

	/* Verify KVM cleared OSXSAVE in CPUID when it was cleared in CR4. */
	GUEST_ASSERT(!(regs[X86_FEATURE_OSXSAVE.reg] & BIT(X86_FEATURE_OSXSAVE.bit)));

	/* Verify restoring CR4 also restored OSXSAVE in CPUID. */
	GUEST_ASSERT(this_cpu_has(X86_FEATURE_OSXSAVE));

	GUEST_DONE();
}

int main(int argc, char *argv[])
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct ucall uc;

	TEST_REQUIRE(is_pkvm_enabled());
	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_XSAVE));

	vm = vm_create_shape_with_one_vcpu(VM_SHAPE_PKVM_PROTECTED, &vcpu,
					   guest_code);

	while (1) {
		vcpu_run(vcpu);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
			break;
		case UCALL_DONE:
			goto done;
		default:
			TEST_FAIL("Unexpected exit: %s",
				exit_reason_str(vcpu->run->exit_reason));
		}
	}

done:
	kvm_vm_free(vm);
	return 0;
}
