// SPDX-License-Identifier: GPL-2.0-only
/*
 * Guest-side MSR access tests for pKVM protected VM.
 */

#include <asm/msr-index.h>
#include <stdint.h>

#include "kvm_util.h"
#include "processor.h"
#include "pkvm/pkvm_util.h"

/* Use HYPERVISOR for MSRs that are emulated unconditionally. */
#define X86_FEATURE_NONE X86_FEATURE_HYPERVISOR

struct kvm_msr {
	const struct kvm_x86_cpu_feature feature;
	const struct kvm_x86_cpu_feature feature2;
	const char *name;
	const u64 reset_val;
	const u64 write_val;
	const u64 rsvd_val;
	const u32 index;
	const bool is_kvm_defined;
};

#define ____MSR_TEST(msr, str, val, rsvd, reset, feat, f2, is_kvm) \
{ \
	.index = msr, \
	.name = str, \
	.write_val = val, \
	.rsvd_val = rsvd, \
	.reset_val = reset, \
	.feature = X86_FEATURE_ ##feat, \
	.feature2 = X86_FEATURE_ ##f2, \
	.is_kvm_defined = is_kvm, \
}

#define __MSR_TEST(msr, str, val, rsvd, reset, feat) \
	____MSR_TEST(msr, str, val, rsvd, reset, feat, feat, false)

#define MSR_TEST_NON_ZERO(msr, val, rsvd, reset, feat) \
	__MSR_TEST(msr, #msr, val, rsvd, reset, feat)

#define MSR_TEST(msr, val, rsvd, feat) \
	__MSR_TEST(msr, #msr, val, rsvd, 0, feat)

#define MSR_TEST2(msr, val, rsvd, feat, f2) \
	____MSR_TEST(msr, #msr, val, rsvd, 0, feat, f2, false)

/*
 * Use a page-aligned canonical value so it's compatible with MSRs that use
 * bits 11:0 for fields other than addresses.
 */
static const u64 canonical_val = 0x123456789000ull;

/*
 * Non-canonical value with bits set in each byte, but not all bits set.
 */
static const u64 u64_val = 0xaaaa5555aaaa5555ull;

#define MSR_TEST_CANONICAL(msr, feat) \
	__MSR_TEST(msr, #msr, canonical_val, NONCANONICAL, 0, feat)

#define MISC_ENABLES_RESET_VAL (MSR_IA32_MISC_ENABLE_PEBS_UNAVAIL | \
					MSR_IA32_MISC_ENABLE_BTS_UNAVAIL)

/* Keep enough room for future additions. */
static struct kvm_msr msrs[128];
static bool ignore_unsupported_msrs;

struct shared_idx {
	u64 gpa;
	bool shared;
	u32 value;
};

static struct shared_idx *shared_idx_ptr;

static u64 fixup_rdmsr_val(u32 msr, u64 want)
{
	/* Match AMD behavior for MSRs that truncate 63:32. */
	if (!host_cpu_is_amd)
		return want;

	switch (msr) {
	case MSR_IA32_SYSENTER_ESP:
	case MSR_IA32_SYSENTER_EIP:
	case MSR_TSC_AUX:
		return want & GENMASK_ULL(31, 0);
	default:
		return want;
	}
}

static void __rdmsr(u32 msr, u64 want)
{
	u64 val;
	u8 vec;

	vec = rdmsr_safe(msr, &val);
	__GUEST_ASSERT(!vec, "Unexpected %s on RDMSR(0x%x)", ex_str(vec), msr);

	__GUEST_ASSERT(val == want, "Wanted 0x%lx from RDMSR(0x%x), got 0x%lx",
		       want, msr, val);
}

static void __wrmsr(u32 msr, u64 val)
{
	u8 vec;

	vec = wrmsr_safe(msr, val);
	__GUEST_ASSERT(!vec, "Unexpected %s on WRMSR(0x%x, 0x%lx)",
		       ex_str(vec), msr, val);
	__rdmsr(msr, fixup_rdmsr_val(msr, val));
}

static void guest_test_supported_msr(const struct kvm_msr *msr)
{
	__rdmsr(msr->index, msr->reset_val);
	__wrmsr(msr->index, msr->write_val);
	__wrmsr(msr->index, msr->reset_val);
}

static void guest_test_unsupported_msr(const struct kvm_msr *msr)
{
	u64 val;
	u8 vec;

	/* KVM ignore_msrs behavior is inconsistent, skip unsupported checks. */
	if (ignore_unsupported_msrs)
		return;

	/*
	 * {S,U}_CET exist if IBT or SHSTK is supported, but with writable bits
	 * tied to the specific supported feature.
	 */
	if (this_cpu_has(msr->feature2)) {
		if (msr->index != MSR_IA32_U_CET && msr->index != MSR_IA32_S_CET)
			return;
		goto wrmsr_gp;
	}

	vec = rdmsr_safe(msr->index, &val);
	__GUEST_ASSERT(vec == GP_VECTOR, "Wanted #GP on RDMSR(0x%x), got %s",
		       msr->index, ex_str(vec));

wrmsr_gp:
	vec = wrmsr_safe(msr->index, msr->write_val);
	__GUEST_ASSERT(vec == GP_VECTOR, "Wanted #GP on WRMSR(0x%x, 0x%lx), got %s",
		       msr->index, msr->write_val, ex_str(vec));
}

static void guest_test_reserved_val(const struct kvm_msr *msr)
{
	u8 vec;

	if (ignore_unsupported_msrs)
		return;

	/* If CPU truncates the value, expect success with truncation. */
	if (!this_cpu_has(msr->feature) ||
	    msr->rsvd_val == fixup_rdmsr_val(msr->index, msr->rsvd_val)) {
		vec = wrmsr_safe(msr->index, msr->rsvd_val);
		__GUEST_ASSERT(vec == GP_VECTOR,
			       "Wanted #GP on WRMSR(0x%x, 0x%lx), got %s",
			       msr->index, msr->rsvd_val, ex_str(vec));
	} else {
		__wrmsr(msr->index, msr->rsvd_val);
		__wrmsr(msr->index, msr->reset_val);
	}
}

static void guest_main(void)
{
	const struct kvm_msr *msr;
	u32 i;

	if (!shared_idx_ptr->shared) {
		pkvm_guest_share_mem(shared_idx_ptr, shared_idx_ptr->gpa,
				    sizeof(*shared_idx_ptr));
		shared_idx_ptr->shared = true;
	}

	/* Handshake so host starts writing idx only after memory is shared. */
	GUEST_SYNC(1);

	for (;;) {
		i = READ_ONCE(shared_idx_ptr->value);
		__GUEST_ASSERT(i < ARRAY_SIZE(msrs), "Invalid idx %u", i);
		msr = &msrs[i];

		if (this_cpu_has(msr->feature))
			guest_test_supported_msr(msr);
		else
			guest_test_unsupported_msr(msr);

		if (msr->rsvd_val)
			guest_test_reserved_val(msr);

		GUEST_SYNC(0);
	}
}

static void run_vcpu_once(struct kvm_vcpu *vcpu)
{
	struct ucall uc;

	vcpu_run(vcpu);

	switch (get_ucall(vcpu, &uc)) {
	case UCALL_SYNC:
		return;
	case UCALL_ABORT:
		REPORT_GUEST_ASSERT(uc);
	default:
		TEST_FAIL("Unexpected exit: %s", exit_reason_str(vcpu->run->exit_reason));
	}
}

static void test_msrs_guest_only(void)
{
	const struct kvm_msr __msrs[] = {
		MSR_TEST_NON_ZERO(MSR_IA32_MISC_ENABLE,
				  MISC_ENABLES_RESET_VAL | MSR_IA32_MISC_ENABLE_FAST_STRING,
				  MSR_IA32_MISC_ENABLE_FAST_STRING, MISC_ENABLES_RESET_VAL, NONE),
		MSR_TEST_NON_ZERO(MSR_IA32_CR_PAT, 0x07070707, 0, 0x7040600070406, NONE),

		/* TSC_AUX is supported if RDTSCP or RDPID is supported. */
		MSR_TEST2(MSR_TSC_AUX, 0x12345678, u64_val, RDTSCP, RDPID),
		MSR_TEST2(MSR_TSC_AUX, 0x12345678, u64_val, RDPID, RDTSCP),

		MSR_TEST(MSR_IA32_SYSENTER_CS, 0x1234, 0, NONE),
		MSR_TEST(MSR_IA32_SYSENTER_ESP, canonical_val, 0, NONE),
		MSR_TEST(MSR_IA32_SYSENTER_EIP, canonical_val, 0, NONE),

		MSR_TEST_CANONICAL(MSR_FS_BASE, LM),
		MSR_TEST_CANONICAL(MSR_GS_BASE, LM),
		MSR_TEST_CANONICAL(MSR_KERNEL_GS_BASE, LM),
		MSR_TEST_CANONICAL(MSR_LSTAR, LM),
		MSR_TEST_CANONICAL(MSR_CSTAR, LM),
		MSR_TEST(MSR_SYSCALL_MASK, 0xffffffff, 0, LM),

		MSR_TEST2(MSR_IA32_S_CET, CET_SHSTK_EN, CET_RESERVED, SHSTK, IBT),
		MSR_TEST2(MSR_IA32_S_CET, CET_ENDBR_EN, CET_RESERVED, IBT, SHSTK),
		MSR_TEST2(MSR_IA32_U_CET, CET_SHSTK_EN, CET_RESERVED, SHSTK, IBT),
		MSR_TEST2(MSR_IA32_U_CET, CET_ENDBR_EN, CET_RESERVED, IBT, SHSTK),
		MSR_TEST_CANONICAL(MSR_IA32_PL0_SSP, SHSTK),
		MSR_TEST(MSR_IA32_PL0_SSP, canonical_val, canonical_val | 1, SHSTK),
		MSR_TEST_CANONICAL(MSR_IA32_PL1_SSP, SHSTK),
		MSR_TEST(MSR_IA32_PL1_SSP, canonical_val, canonical_val | 1, SHSTK),
		MSR_TEST_CANONICAL(MSR_IA32_PL2_SSP, SHSTK),
		MSR_TEST(MSR_IA32_PL2_SSP, canonical_val, canonical_val | 1, SHSTK),
		MSR_TEST_CANONICAL(MSR_IA32_PL3_SSP, SHSTK),
		MSR_TEST(MSR_IA32_PL3_SSP, canonical_val, canonical_val | 1, SHSTK),
	};
	struct kvm_vm *vm;
	struct kvm_vcpu *vcpu;
	vm_vaddr_t idx_gva;
	struct shared_idx *idx_hva;

	kvm_static_assert(sizeof(__msrs) <= sizeof(msrs));
	kvm_static_assert(ARRAY_SIZE(__msrs) <= ARRAY_SIZE(msrs));
	memcpy(msrs, __msrs, sizeof(__msrs));

	ignore_unsupported_msrs = kvm_is_ignore_msrs();

	vm = vm_create_shape_with_one_vcpu(VM_SHAPE_PKVM_PROTECTED, &vcpu, guest_main);

	sync_global_to_guest(vm, msrs);
	sync_global_to_guest(vm, ignore_unsupported_msrs);

	idx_gva = vm_vaddr_alloc(vm, sizeof(*idx_hva), KVM_UTIL_MIN_VADDR);
	idx_hva = addr_gva2hva(vm, idx_gva);
	memset(idx_hva, 0, sizeof(*idx_hva));
	idx_hva->gpa = addr_hva2gpa(vm, idx_hva);
	write_guest_global(vm, shared_idx_ptr, (struct shared_idx *)idx_gva);

	/* Wait for guest to share idx memory before host writes it. */
	run_vcpu_once(vcpu);

	for (u32 i = 0; i < ARRAY_SIZE(__msrs); i++) {
		if (msrs[i].is_kvm_defined)
			continue;

		idx_hva->value = i;

		run_vcpu_once(vcpu);
	}

	kvm_vm_free(vm);
}

int main(void)
{
	TEST_REQUIRE(is_pkvm_enabled());

	test_msrs_guest_only();
}
