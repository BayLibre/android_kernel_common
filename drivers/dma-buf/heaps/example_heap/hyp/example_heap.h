#ifndef __EXAMPLE_HEAP_H
#define __EXAMPLE_HEAP_H

#include <linux/types.h>

struct example_heap_range {
	phys_addr_t start;
	phys_addr_t size;
};

struct example_heap_config {
	/* Physical address of the 64-byte secret */
	phys_addr_t token_paddr;
	u32 nr_ranges;
	/* ranges that this protection_id can access */
	struct example_heap_range ranges[16];
};

#ifdef __KVM_NVHE_HYPERVISOR__

extern struct example_heap_config ex_heap_config;

struct module_heap_config {
	const struct example_heap_config *config;
	unsigned long token_haddr;
	pkvm_handle_t bound_handle;
};

int hyp_init(const struct pkvm_module_ops *__ops);
void protect_page(struct user_pt_regs *regs);
void unprotect_page(struct user_pt_regs *regs);
#else
int __kvm_nvhe_hyp_init(const struct pkvm_module_ops *__ops);
void __kvm_nvhe_protect_page(struct user_pt_regs *regs);
void __kvm_nvhe_unprotect_page(struct user_pt_regs *regs);
extern struct example_heap_config __kvm_nvhe_ex_heap_config;


extern unsigned long protect_page_hvc;
extern unsigned long unprotect_page_hvc;
extern const struct dma_heap_ops system_heap_modified_ops;

#define PKVM_VENDOR_IOC_MAGIC		'P'
#define PKVM_VENDOR_IOCTL_ENABLE_SMC	_IOWR(PKVM_VENDOR_IOC_MAGIC, 0x0, __u32)
#endif

#endif
