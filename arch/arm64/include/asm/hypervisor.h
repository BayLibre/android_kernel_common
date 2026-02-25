/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ARM64_HYPERVISOR_H
#define _ASM_ARM64_HYPERVISOR_H

#include <asm/xen/hypervisor.h>

void kvm_init_hyp_services(void);
bool kvm_arm_hyp_service_available(u32 func_id);
void kvm_arm_target_impl_cpu_init(void);

struct device;

#ifdef CONFIG_ARM_PKVM_GUEST
void pkvm_init_hyp_services(void);
bool pkvm_is_guest_device(struct device *dev);
#else
static inline void pkvm_init_hyp_services(void) { };
bool pkvm_is_guest_device(struct device *dev)
{
	return false;
}
#endif

static inline void kvm_arch_init_hyp_services(void)
{
	pkvm_init_hyp_services();
};

#endif
