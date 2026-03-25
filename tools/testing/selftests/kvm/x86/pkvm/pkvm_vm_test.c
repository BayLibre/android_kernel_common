// SPDX-License-Identifier: GPL-2.0-only

#include "processor.h"
#include "kvm_util.h"
#include "pkvm/pkvm_util.h"
#include "ucall_common.h"
#include "kselftest_harness.h"

static void guest_code_lifecycle(void)
{
	GUEST_DONE();
}

TEST(verify_pkvm_protected_vm_lifecycle)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct ucall uc;

	vm = vm_create_shape_with_one_vcpu(VM_SHAPE_PKVM_PROTECTED, &vcpu,
					   guest_code_lifecycle);

	vcpu_run(vcpu);
	TEST_ASSERT_EQ(get_ucall(vcpu, &uc), UCALL_DONE);

	kvm_vm_free(vm);
}

int main(int argc, char **argv)
{
	TEST_REQUIRE(is_pkvm_enabled());
	return test_harness_run(argc, argv);
}