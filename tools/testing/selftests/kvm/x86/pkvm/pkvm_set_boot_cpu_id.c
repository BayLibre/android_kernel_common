// SPDX-License-Identifier: GPL-2.0-only
/*
 * Test that KVM_SET_BOOT_CPU_ID works as intended (pKVM variant)
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "asm/kvm.h"
#include <linux/kvm_para.h>
#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "apic.h"
#include "pkvm/pkvm_boot.h"
#include "pkvm/pkvm_util.h"

static void guest_bsp_vcpu(void *arg)
{
	u32 apic_id = (u32)(uintptr_t)arg;

	GUEST_ASSERT(kvm_hypercall(PKVM_GHC_START_CPU, apic_id,
				   PKVM_AP_SIPI_BOOT_GPA, 0, 0) == 0);

	GUEST_SYNC(1);

	GUEST_ASSERT_NE(get_bsp_flag(), 0);

	GUEST_DONE();
}

static void guest_not_bsp_vcpu(void *arg)
{
	GUEST_SYNC(1);

	GUEST_ASSERT_EQ(get_bsp_flag(), 0);

	GUEST_DONE();
}

static void test_set_invalid_bsp(struct kvm_vm *vm)
{
	unsigned long max_vcpu_id = vm_check_cap(vm, KVM_CAP_MAX_VCPU_ID);
	int r;

	if (max_vcpu_id) {
		r = __vm_ioctl(vm, KVM_SET_BOOT_CPU_ID, (void *)(max_vcpu_id + 1));
		TEST_ASSERT(r == -1 && errno == EINVAL,
			    "BSP with ID > MAX should fail");
	}

	r = __vm_ioctl(vm, KVM_SET_BOOT_CPU_ID, (void *)(1L << 32));
	TEST_ASSERT(r == -1 && errno == EINVAL,
		    "BSP with ID[63:32]!=0 should fail");
}

static void test_set_bsp_busy(struct kvm_vcpu *vcpu, const char *msg)
{
	int r = __vm_ioctl(vcpu->vm, KVM_SET_BOOT_CPU_ID,
			   (void *)(unsigned long)vcpu->id);

	TEST_ASSERT(r == -1 && errno == EBUSY,
		    "KVM_SET_BOOT_CPU_ID set %s", msg);
}

static void run_vcpu(struct kvm_vcpu *vcpu)
{
	struct ucall uc;
	int stage;

	for (stage = 0; stage < 2; stage++) {
		vcpu_run(vcpu);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_SYNC:
			TEST_ASSERT(!strcmp((const char *)uc.args[0], "hello") &&
				    uc.args[1] == stage + 1,
				    "Stage %d: Unexpected register values vmexit, got %lx",
				    stage + 1, (unsigned long)uc.args[1]);
			test_set_bsp_busy(vcpu, "while running vm");
			break;
		case UCALL_DONE:
			TEST_ASSERT(stage == 1,
				    "Expected GUEST_DONE in stage 2, got stage %d",
				    stage);
			break;
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
		default:
			TEST_ASSERT(false, "Unexpected exit: %s",
			    exit_reason_str(vcpu->run->exit_reason));
		}
	}
}

static struct kvm_vm *create_vm(uint32_t nr_vcpus, uint32_t bsp_vcpu_id,
				struct kvm_vcpu *vcpus[])
{
	struct kvm_vm *vm;
	u32 ap_vcpu_id = bsp_vcpu_id ^ 1;
	uint32_t i;

	vm = __vm_create(VM_SHAPE_PKVM_PROTECTED, nr_vcpus, 0);

	test_set_invalid_bsp(vm);

	vm_set_boot_cpu_id(vm, bsp_vcpu_id);

	for (i = 0; i < nr_vcpus; i++)
		vcpus[i] = vm_vcpu_add(vm, i,
				      i == bsp_vcpu_id ? guest_bsp_vcpu :
				      guest_not_bsp_vcpu);

	vcpu_args_set(vcpus[bsp_vcpu_id], 1, ap_vcpu_id);
	return vm;
}

static void run_vm_bsp(uint32_t bsp_vcpu_id)
{
	struct kvm_vcpu *vcpus[2];
	uint32_t ap_vcpu_id = bsp_vcpu_id ^ 1;
	struct kvm_vm *vm;

	vm = create_vm(ARRAY_SIZE(vcpus), bsp_vcpu_id, vcpus);

	/* In protected mode, APs must be started by the BSP via START_CPU. */
	run_vcpu(vcpus[bsp_vcpu_id]);
	run_vcpu(vcpus[ap_vcpu_id]);

	kvm_vm_free(vm);
}

static void check_set_bsp_busy(void)
{
	struct kvm_vcpu *vcpus[2];
	struct kvm_vm *vm;

	vm = create_vm(ARRAY_SIZE(vcpus), 0, vcpus);

	test_set_bsp_busy(vcpus[1], "after adding vcpu");

	run_vcpu(vcpus[0]);
	run_vcpu(vcpus[1]);

	test_set_bsp_busy(vcpus[1], "to a terminated vcpu");

	kvm_vm_free(vm);
}

int main(int argc, char *argv[])
{
	TEST_REQUIRE(is_pkvm_enabled());
	TEST_REQUIRE(kvm_has_cap(KVM_CAP_SET_BOOT_CPU_ID));

	/*
	 * pKVM AP execution is always mediated through secure START_CPU bringup.
	 * Cover both BSP=0 and BSP=1 in multi-vCPU topology.
	 */
	run_vm_bsp(0);
	run_vm_bsp(1);
	run_vm_bsp(0);

	check_set_bsp_busy();

	return 0;
}
