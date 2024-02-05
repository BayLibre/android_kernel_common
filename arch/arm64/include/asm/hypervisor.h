/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ARM64_HYPERVISOR_H
#define _ASM_ARM64_HYPERVISOR_H

#include <linux/memory.h>
#include <linux/mm.h>

#include <asm/xen/hypervisor.h>

void kvm_init_hyp_services(void);
bool kvm_arm_hyp_service_available(u32 func_id);

struct hypervisor_ops {
#ifdef CONFIG_MEMORY_RELINQUISH
	bool (*page_relinquish_disallowed)(void);
	void (*page_relinquish)(struct page *page);
#endif
};

extern struct hypervisor_ops hyp_ops;

#ifdef CONFIG_ARM_PKVM_GUEST
void pkvm_init_hyp_services(void);
#else
static inline void pkvm_init_hyp_services(void) { };
#endif

static inline void kvm_arch_init_hyp_services(void)
{
	pkvm_init_hyp_services();
};

#endif
