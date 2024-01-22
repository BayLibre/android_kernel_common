/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ARM64_HYPERVISOR_H
#define _ASM_ARM64_HYPERVISOR_H

#include <asm/xen/hypervisor.h>

void kvm_init_hyp_services(void);
bool kvm_arm_hyp_service_available(u32 func_id);
void kvm_arm_init_hyp_services(void);
void kvm_init_memshare_services(void);
void kvm_init_ioremap_services(void);

<<<<<<< HEAD   (df4996 UPSTREAM: erofs: fix infinite loop due to a race of filling )
=======
struct hypervisor_ops {
#ifdef CONFIG_MEMORY_RELINQUISH
	void (*page_relinquish)(struct page *page);
	void (*post_page_relinquish_tlb_inv)(void);
#endif
};

extern struct hypervisor_ops hyp_ops;

>>>>>>> CHANGE (886c9d ANDROID: arm64: virt: Invalidate tlb once the balloon before)
#ifdef CONFIG_MEMORY_RELINQUISH
void kvm_init_memrelinquish_services(void);
#else
static inline void kvm_init_memrelinquish_services(void) {}
#endif

#endif
