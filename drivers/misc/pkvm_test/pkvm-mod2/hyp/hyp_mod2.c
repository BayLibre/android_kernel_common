#include <asm/kvm_pkvm_module.h>

extern int pkvm_mod1_export(void);

void pkvm_mod2_hyp_hvc(struct user_pt_regs *regs)
{
	pkvm_mod1_export();

	regs->regs[1] = 0;
	regs->regs[0] = 0;
}

int pkvm_mod2_hyp_init(const struct pkvm_module_ops *ops)
{
	return 0;
}
