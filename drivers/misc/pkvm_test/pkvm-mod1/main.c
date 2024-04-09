#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <asm/kvm_pkvm_module.h>

#define HYP_EVENT_FILE ../../../../drivers/misc/pkvm_test/pkvm-mod1/hyp/events.h
#include <asm/kvm_define_hypevents.h>

int __kvm_nvhe_pkvm_mod1_hyp_init(const struct pkvm_module_ops *ops);

int __kvm_nvhe_pkvm_mod1_export(void);
EXPORT_SYMBOL(__kvm_nvhe_pkvm_mod1_export);

static int __init pkvm_mod1_init(void)
{
	unsigned long token;
	int ret;

	ret = pkvm_load_el2_module(__kvm_nvhe_pkvm_mod1_hyp_init, &token);
	if (ret)
		return ret;
	return 0;
}
module_init(pkvm_mod1_init);
MODULE_LICENSE("GPL");
