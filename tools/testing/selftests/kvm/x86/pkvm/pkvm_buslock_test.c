// SPDX-License-Identifier: GPL-2.0-only
#include <linux/atomic.h>

#include "kvm_util.h"
#include "processor.h"
#include "pkvm/pkvm_util.h"
#include "test_util.h"

#define NR_BUS_LOCKS_PER_LEVEL 100
#define CACHE_LINE_SIZE		64

struct shared_buslock {
	u64 gpa;
	bool shared;
	u8 buffer[CACHE_LINE_SIZE * 2] __aligned(CACHE_LINE_SIZE);
};

static struct shared_buslock *shared_buslock_ptr;
static atomic_t *val;

static void guest_share_buslock_buffer(void)
{
	if (shared_buslock_ptr->shared)
		return;

	pkvm_guest_share_mem(shared_buslock_ptr, shared_buslock_ptr->gpa,
			    sizeof(*shared_buslock_ptr));
	shared_buslock_ptr->shared = true;
}

static void guest_generate_buslocks(void)
{
	for (int i = 0; i < NR_BUS_LOCKS_PER_LEVEL; i++)
		atomic_inc(val);
}

static void guest_code(void *unused)
{
	guest_share_buslock_buffer();
	val = (void *)&shared_buslock_ptr->buffer[CACHE_LINE_SIZE - (sizeof(*val) / 2)];

	guest_generate_buslocks();
	GUEST_DONE();
}

int main(int argc, char *argv[])
{
	const int expected_bus_locks = NR_BUS_LOCKS_PER_LEVEL;
	struct kvm_vcpu *vcpu;
	struct kvm_run *run;
	struct kvm_vm *vm;
	struct shared_buslock *buslock_hva;
	vm_vaddr_t buslock_gva;
	atomic_t *shared_val;
	int bus_locks = 0;

	TEST_REQUIRE(is_pkvm_enabled());
	TEST_REQUIRE(host_cpu_is_intel);
	TEST_REQUIRE(kvm_has_cap(KVM_CAP_X86_BUS_LOCK_EXIT));

	vm = __vm_create(VM_SHAPE_PKVM_PROTECTED, 1, 0);
	vm_enable_cap(vm, KVM_CAP_X86_BUS_LOCK_EXIT, KVM_BUS_LOCK_DETECTION_EXIT);
	vcpu = vm_vcpu_add(vm, 0, guest_code);
	run = vcpu->run;

	buslock_gva = vm_vaddr_alloc(vm, sizeof(*buslock_hva), KVM_UTIL_MIN_VADDR);
	buslock_hva = addr_gva2hva(vm, buslock_gva);
	memset(buslock_hva, 0, sizeof(*buslock_hva));
	buslock_hva->gpa = addr_hva2gpa(vm, buslock_hva);
	shared_val = (void *)&buslock_hva->buffer[CACHE_LINE_SIZE - (sizeof(*shared_val) / 2)];
	write_guest_global(vm, shared_buslock_ptr, (struct shared_buslock *)buslock_gva);

	while (true) {
		struct ucall uc;

		vcpu_run(vcpu);

		if (run->exit_reason == KVM_EXIT_MMIO) {
			/* pKVM UCALL exits are MMIO-backed and decoded by get_ucall(). */
			switch (get_ucall(vcpu, &uc)) {
			case UCALL_ABORT:
				REPORT_GUEST_ASSERT(uc);
				goto done;
			case UCALL_DONE:
				goto done;
			default:
				TEST_FAIL("Unknown ucall 0x%lx.", uc.cmd);
			}
		}

		TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_X86_BUS_LOCK);

		TEST_ASSERT_EQ(atomic_read(shared_val), bus_locks + host_cpu_is_intel);

		bus_locks++;
	}

done:
	TEST_ASSERT_EQ(bus_locks, expected_bus_locks);
	TEST_ASSERT_EQ(atomic_read(shared_val), expected_bus_locks);

	kvm_vm_free(vm);
	return 0;
}
