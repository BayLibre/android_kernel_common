// SPDX-License-Identifier: GPL-2.0-only

#include "processor.h"
#include "kvm_util.h"
#include "pkvm/pkvm_boot.h"
#include "pkvm/pkvm_util.h"
#include "ucall_common.h"
#include "kselftest_harness.h"

#define NUM_APS 3
#define AP_STARTED_MAGIC 0xfeed0000ULL

static void guest_code_lifecycle(void)
{
	GUEST_DONE();
}

static void guest_code_ap(void)
{
	GUEST_SYNC(AP_STARTED_MAGIC);
	GUEST_DONE();
}

static void guest_code_bsp(void)
{
	int i;

	for (i = 0; i < NUM_APS; i++) {
		u32 apic_id = i + 1;

		/*
		 * The hypercall tells the host to start the AP at the
		 * SIPI-targeted pKVM AP boot page.
		 */
		GUEST_ASSERT(kvm_hypercall(PKVM_GHC_START_CPU, apic_id,
					    PKVM_AP_SIPI_BOOT_GPA, 0, 0) == 0);
		GUEST_SYNC(i);
	}

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

TEST(verify_pkvm_ap_bringup)
{
	struct kvm_vcpu *bsp;
	struct kvm_vcpu *aps[NUM_APS];
	struct kvm_vm *vm;
	struct ucall uc;
	int i;

	/*
	 * Create a protected VM, then add the BSP and APs.
	 * The AP guest function is registered up front; the BSP then kicks
	 * APs via hypercall using the fixed SIPI boot page base.
	 */
	vm = __vm_create(VM_SHAPE_PKVM_PROTECTED, NUM_APS + 1, 0);

	bsp = vm_vcpu_add(vm, 0, guest_code_bsp);

	for (i = 0; i < NUM_APS; i++)
		aps[i] = vm_vcpu_add(vm, i + 1, guest_code_ap);

	/*
	 * Run the BSP to kick off the APs, and then run the APs to verify
	 * they started correctly.
	 */
	for (i = 0; i < NUM_APS; i++) {
		vcpu_run(bsp);
		TEST_ASSERT_EQ(get_ucall(bsp, &uc), UCALL_SYNC);
		TEST_ASSERT_EQ(uc.args[1], i);

		vcpu_run(aps[i]);
		TEST_ASSERT_EQ(get_ucall(aps[i], &uc), UCALL_SYNC);
		TEST_ASSERT_EQ(uc.args[1], AP_STARTED_MAGIC);

		vcpu_run(aps[i]);
		TEST_ASSERT_EQ(get_ucall(aps[i], &uc), UCALL_DONE);
	}

	vcpu_run(bsp);
	TEST_ASSERT_EQ(get_ucall(bsp, &uc), UCALL_DONE);

	kvm_vm_free(vm);
}

int main(int argc, char **argv)
{
	TEST_REQUIRE(is_pkvm_enabled());
	return test_harness_run(argc, argv);
}