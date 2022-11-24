#include <asm/kvm_pgtable.h>

#define HCALL_HANDLED 0
#define HCALL_UNHANDLED -1

#ifdef CONFIG_MODULES
int __pkvm_init_module(unsigned long args_hva);
int __pkvm_register_hcall(unsigned long hfn_kern_va, unsigned long module_id);
int handle_host_dynamic_hcall(struct kvm_cpu_context *host_ctxt);
void pkvm_modules_lock(void);
void pkvm_modules_unlock(void);
bool pkvm_modules_enabled(void);
int __pkvm_close_module_registration(void);
#else
static inline int __pkvm_init_module(unsigned long args_hva)
{
	return -EOPNOTSUPP;
}
static inline int
__pkvm_register_hcall(unsigned long hfn_kern_va, unsigned long module_id)
{
	return -EOPNOTSUPP;
}
static inline int handle_host_dynamic_hcall(struct kvm_cpu_context *host_ctxt)
{
	return HCALL_UNHANDLED;
}
void pkvm_modules_lock(void) { }
void pkvm_modules_unlock(void) { }
bool pkvm_modules_enabled(void) { return false; }
int __pkvm_close_module_registration(void) { return -EOPNOTSUPP; }
#endif
