#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <asm/kvm_pkvm_module.h>

static int hvc_number;

int __kvm_nvhe_pkvm_mod2_hyp_init(const struct pkvm_module_ops *ops);
void __kvm_nvhe_pkvm_mod2_hyp_hvc(struct user_pt_regs *regs);

static int hvc_open(struct inode *inode, struct file *f)
{
	return pkvm_el2_mod_call(hvc_number);
}

ssize_t hvc_write(struct file *f, const char __user *buf, size_t len, loff_t *off)
{
	return len;
}

static const struct file_operations hvc_fops = {
	.open = hvc_open,
	.read = NULL,
	.write = hvc_write,
	.llseek = default_llseek,
};

static int __init pkvm_mod2_init(void)
{
	unsigned long token;
	int ret;

	ret = pkvm_load_el2_module(__kvm_nvhe_pkvm_mod2_hyp_init, &token);
	if (ret)
		return ret;

	ret = pkvm_register_el2_mod_call(__kvm_nvhe_pkvm_mod2_hyp_hvc, token);
	if (ret < 0)
		return ret;

	hvc_number = ret;

	debugfs_create_file("pkvm_mod2", 0200, NULL, NULL, &hvc_fops);

	return 0;
}
module_init(pkvm_mod2_init);
MODULE_LICENSE("GPL");
