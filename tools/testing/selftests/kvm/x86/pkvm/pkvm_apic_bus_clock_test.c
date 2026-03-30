// SPDX-License-Identifier: GPL-2.0-only
/*
 * Verify KVM correctly emulates the APIC bus frequency when the VMM configures
 * the frequency via KVM_CAP_X86_APIC_BUS_CYCLES_NS (pKVM variant).
 */

#include "apic.h"
#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "pkvm/pkvm_util.h"

/*
 * Possible TDCR values with matching divide count. Used to modify APIC
 * timer frequency.
 */
static const struct {
	const uint32_t tdcr;
	const uint32_t divide_count;
} tdcrs[] = {
	{0x0, 2},
	{0x1, 4},
	{0x2, 8},
	{0x3, 16},
	{0x8, 32},
	{0x9, 64},
	{0xa, 128},
	{0xb, 1},
};

static uint32_t apic_read_reg(unsigned int reg)
{
	return x2apic_read_reg(reg);
}

static void apic_write_reg(unsigned int reg, uint32_t val)
{
	x2apic_write_reg(reg, val);
}

static void apic_guest_code(uint64_t apic_hz, uint64_t delay_ms)
{
	uint64_t tsc_hz = guest_tsc_khz * 1000;
	const uint32_t tmict = ~0u;
	uint64_t tsc0, tsc1, freq;
	uint32_t tmcct;
	int i;

	x2apic_enable();

	/*
	 * Setup one-shot timer.  The vector does not matter because the
	 * interrupt should not fire.
	 */
	apic_write_reg(APIC_LVTT, APIC_LVT_TIMER_ONESHOT | APIC_LVT_MASKED);

	for (i = 0; i < ARRAY_SIZE(tdcrs); i++) {
		apic_write_reg(APIC_TDCR, tdcrs[i].tdcr);
		apic_write_reg(APIC_TMICT, tmict);

		tsc0 = rdtsc();
		udelay(delay_ms * 1000);
		tmcct = apic_read_reg(APIC_TMCCT);
		tsc1 = rdtsc();

		/*
		 * Stop the timer _after_ reading the current, final count, as
		 * writing the initial counter also modifies the current count.
		 */
		apic_write_reg(APIC_TMICT, 0);

		freq = (tmict - tmcct) * tdcrs[i].divide_count * tsc_hz / (tsc1 - tsc0);
		/* Check if measured frequency is within 5% of configured frequency. */
		__GUEST_ASSERT(freq < apic_hz * 105 / 100 && freq > apic_hz * 95 / 100,
			       "Frequency = %lu (wanted %lu - %lu), bus = %lu, div = %u, tsc = %lu",
			       freq, apic_hz * 95 / 100, apic_hz * 105 / 100,
			       apic_hz, tdcrs[i].divide_count, tsc_hz);
	}

	GUEST_DONE();
}

static void test_apic_bus_clock(struct kvm_vcpu *vcpu)
{
	bool done = false;
	struct ucall uc;

	while (!done) {
		vcpu_run(vcpu);

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_DONE:
			done = true;
			break;
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
			break;
		default:
			TEST_FAIL("Unexpected exit: %s",
				exit_reason_str(vcpu->run->exit_reason));
			break;
		}
	}
}

static void run_apic_bus_clock_test(uint64_t apic_hz, uint64_t delay_ms)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	int ret;

	vm = __vm_create(VM_SHAPE_PKVM_PROTECTED, 1, 0);

	vm_enable_cap(vm, KVM_CAP_X86_APIC_BUS_CYCLES_NS,
		      NSEC_PER_SEC / apic_hz);

	vcpu = vm_vcpu_add(vm, 0, apic_guest_code);
	vcpu_args_set(vcpu, 2, apic_hz, delay_ms);

	ret = __vm_enable_cap(vm, KVM_CAP_X86_APIC_BUS_CYCLES_NS,
			      NSEC_PER_SEC / apic_hz);
	TEST_ASSERT(ret < 0 && errno == EINVAL,
		    "Setting APIC bus frequency after vCPU creation should fail.");

	test_apic_bus_clock(vcpu);
	kvm_vm_free(vm);
}

static void help(char *name)
{
	puts("");
	printf("usage: %s [-h] [-d delay] [-f APIC bus freq]\n", name);
	puts("");
	printf("-d: Delay (in msec) guest uses to measure APIC bus frequency.\n");
	printf("-f: The APIC bus frequency (in MHz) to be configured for the guest.\n");
	puts("");
}

int main(int argc, char *argv[])
{
	/*
	 * Arbitrarily default to 25MHz for the APIC bus frequency, which is
	 * different enough from the default 1GHz to be interesting.
	 */
	uint64_t apic_hz = 25 * 1000 * 1000;
	uint64_t delay_ms = 100;
	int opt;

	TEST_REQUIRE(is_pkvm_enabled());
	TEST_REQUIRE(kvm_has_cap(KVM_CAP_X86_APIC_BUS_CYCLES_NS));
	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_X2APIC));

	while ((opt = getopt(argc, argv, "d:f:h")) != -1) {
		switch (opt) {
		case 'f':
			apic_hz = atoi_positive("APIC bus frequency", optarg) * 1000 * 1000;
			break;
		case 'd':
			delay_ms = atoi_positive("Delay in milliseconds", optarg);
			break;
		case 'h':
		default:
			help(argv[0]);
			exit(KSFT_SKIP);
		}
	}

	run_apic_bus_clock_test(apic_hz, delay_ms);
}
