// SPDX-License-Identifier: GPL-2.0-only
#ifndef __GIC_V3_ITS_PKVM_GIC_V3_ITS__
#define __GIC_V3_ITS_PKVM_GIC_V3_ITS__

#ifdef MODULE
#define hyp_gic_v3_its_init kvm_nvhe_sym(hyp_gic_v3_its_init)
int hyp_gic_v3_its_init(const struct pkvm_module_ops *ops);

#define hyp_gic_v3_its_protect_hvc kvm_nvhe_sym(hyp_gic_v3_its_protect_hvc)
void hyp_gic_v3_its_protect_hvc(struct user_pt_regs *regs);

#define hyp_gic_v3_redist_protect_hvc \
	kvm_nvhe_sym(hyp_gic_v3_redist_protect_hvc)
void hyp_gic_v3_redist_protect_hvc(struct user_pt_regs *regs);

#endif /* MODULE */

#endif /* __GIC_V3_ITS_PKVM_GIC_V3_ITS__ */
