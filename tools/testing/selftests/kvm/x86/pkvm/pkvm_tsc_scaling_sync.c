// SPDX-License-Identifier: GPL-2.0-only
/*
 * pKVM variant of tsc_scaling_sync.
 */

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "pkvm/pkvm_util.h"
#include "pkvm/pkvm_boot.h"

#include <stdint.h>
#include <time.h>
#include <sched.h>
#include <signal.h>
#include <pthread.h>

#define NR_TEST_VCPUS 20

static struct kvm_vm *vm;
static struct kvm_vcpu *vcpus[NR_TEST_VCPUS];

static pthread_mutex_t ap_start_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ap_start_cond = PTHREAD_COND_INITIALIZER;
static bool ap_can_run[NR_TEST_VCPUS];

#define TEST_TSC_KHZ    2345678UL
#define TEST_TSC_OFFSET 200000000

#define UCALL_AP_WOKEN     1
#define UCALL_AP_WAKE_DONE 2
#define UCALL_TSC_FAILURES 3
#define UCALL_AP_START_FAIL 4

uint64_t tsc_sync;

static void tsc_sync_guest_loop(void)
{
	uint64_t start_tsc, local_tsc, tmp;
	u64 failures = 0;

	start_tsc = rdtsc();
	do {
		tmp = READ_ONCE(tsc_sync);
		local_tsc = rdtsc();
		WRITE_ONCE(tsc_sync, local_tsc);
		if (unlikely(local_tsc < tmp))
			failures++;

	} while (local_tsc - start_tsc < 5000 * TEST_TSC_KHZ);

	GUEST_SYNC_ARGS(UCALL_TSC_FAILURES, failures, 0, 0, 0);

	GUEST_DONE();
}

static void guest_code_bsp(void)
{
	int i;

	for (i = 1; i < NR_TEST_VCPUS; i++) {
		long ret = kvm_hypercall(PKVM_GHC_START_CPU, i,
					 PKVM_AP_SIPI_BOOT_GPA, 0, 0);

		if (ret) {
			GUEST_SYNC_ARGS(UCALL_AP_START_FAIL, i, ret, 0, 0);
			GUEST_DONE();
		}

		GUEST_SYNC_ARGS(UCALL_AP_WOKEN, i, 0, 0, 0);
	}

	GUEST_SYNC_ARGS(UCALL_AP_WAKE_DONE, 0, 0, 0, 0);

	tsc_sync_guest_loop();
}

static void guest_code_ap(void)
{
	tsc_sync_guest_loop();
}

static void *run_vcpu(void *_cpu_nr)
{
	unsigned long vcpu_id = (unsigned long)_cpu_nr;
	unsigned long failures = 0;
	struct kvm_vcpu *vcpu = vcpus[vcpu_id];

	if (vcpu_id) {
		pthread_mutex_lock(&ap_start_lock);
		while (!ap_can_run[vcpu_id])
			pthread_cond_wait(&ap_start_cond, &ap_start_lock);
		pthread_mutex_unlock(&ap_start_lock);
	}

	for (;;) {
		struct ucall uc;
		u64 stage;

		vcpu_run(vcpu);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_DONE:
			goto out;
		case UCALL_SYNC:
			stage = uc.args[1];
			if (stage == UCALL_AP_WOKEN) {
				u32 ap_id = uc.args[2];

				TEST_ASSERT(ap_id > 0 && ap_id < NR_TEST_VCPUS,
					    "Invalid AP id %u", ap_id);
				pthread_mutex_lock(&ap_start_lock);
				ap_can_run[ap_id] = true;
				pthread_cond_broadcast(&ap_start_cond);
				pthread_mutex_unlock(&ap_start_lock);
				break;
			}

			if (stage == UCALL_AP_WAKE_DONE) {
				pthread_mutex_lock(&ap_start_lock);
				for (u32 ap_id = 1; ap_id < NR_TEST_VCPUS; ap_id++)
					ap_can_run[ap_id] = true;
				pthread_cond_broadcast(&ap_start_cond);
				pthread_mutex_unlock(&ap_start_lock);
				break;
			}

			if (stage == UCALL_AP_START_FAIL)
				TEST_FAIL("PKVM_GHC_START_CPU failed for vCPU %lu, rc=%lu",
					  uc.args[2], uc.args[3]);

			if (stage == UCALL_TSC_FAILURES) {
				failures += uc.args[2];
				break;
			}

			TEST_FAIL("Unknown sync stage %lu", stage);
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
		default:
			TEST_FAIL("Unknown ucall %lu, exit_reason=%s", uc.cmd,
				exit_reason_str(vcpu->run->exit_reason));
		}
	}

out:
	return (void *)failures;
}

int main(int argc, char *argv[])
{
	pthread_t cpu_threads[NR_TEST_VCPUS];
	unsigned long failures = 0;
	unsigned long cpu;

	TEST_REQUIRE(is_pkvm_enabled());
	TEST_REQUIRE(kvm_has_cap(KVM_CAP_VM_TSC_CONTROL));

	vm = __vm_create(VM_SHAPE_PKVM_PROTECTED, NR_TEST_VCPUS, 0);
	vm_ioctl(vm, KVM_SET_TSC_KHZ, (void *)TEST_TSC_KHZ);

	for (cpu = 0; cpu < NR_TEST_VCPUS; cpu++)
		vcpus[cpu] = vm_vcpu_add(vm, cpu,
					 cpu ? guest_code_ap : guest_code_bsp);

	for (cpu = 0; cpu < NR_TEST_VCPUS; cpu++)
		vcpu_set_msr(vcpus[cpu], MSR_IA32_TSC, TEST_TSC_OFFSET);

	for (cpu = 0; cpu < NR_TEST_VCPUS; cpu++)
		pthread_create(&cpu_threads[cpu], NULL, run_vcpu, (void *)cpu);

	for (cpu = 0; cpu < NR_TEST_VCPUS; cpu++) {
		void *this_cpu_failures;

		pthread_join(cpu_threads[cpu], &this_cpu_failures);
		failures += (unsigned long)this_cpu_failures;
	}

	TEST_ASSERT(!failures, "TSC sync failed");

	kvm_vm_free(vm);
	return 0;
}
