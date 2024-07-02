// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 - Google Inc
 * Author: Sebastian Ene <sebastianene@google.com>
 * Simple module for pKVM guest SMC handling.
 */
#include <linux/init.h>
#include <linux/file.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <asm/kvm_pkvm_module.h>


#define SMC_PROXY_SET_VM_TRAP_FORWARD	_IOC(_IOC_WRITE, 'k', 1, 0)
#define SMC_PROXY_MODE			(S_IRUGO | S_IWUGO)

static unsigned long pkvm_module_token;
int kvm_nvhe_sym(pkvm_guest_smc_proxy_hyp_init)(const struct pkvm_module_ops *ops);

void kvm_nvhe_sym(pkvm_set_guest_smc_trapping_hyp_hvc)(struct user_pt_regs *regs);

struct smc_proxy_drvdata {
	struct mutex lock;
	int hvc_number;
	struct file *kvm_file;
	struct miscdevice misc;
};

static int smc_proxy_release(struct inode *inode, struct file *file)
{
	struct smc_proxy_drvdata *drvdata =
		container_of(file->private_data, struct smc_proxy_drvdata, misc);

	mutex_lock(&drvdata->lock);
	if (drvdata->kvm_file) {
		fput(drvdata->kvm_file);
		kvm_put_kvm(drvdata->kvm_file->private_data);
	}
	mutex_unlock(&drvdata->lock);
	return 0;
}

static int smc_proxy_get_kvm_file_locked(struct smc_proxy_drvdata *drvdata, unsigned int fd)
{
	struct file *kvm_file;

	if (drvdata->kvm_file)
		return -EPERM;

	kvm_file = fget(fd);
	if (!kvm_file)
		return -EBADF;

	if (!file_is_kvm(kvm_file)) {
		fput(kvm_file);
		return -EINVAL;
	}

	if (!kvm_get_kvm_safe(kvm_file->private_data)) {
		fput(kvm_file);
		return -ENOENT;
	}

	drvdata->kvm_file = kvm_file;
	return 0;
}

static int smc_proxy_set_vm_trap_forward_locked(struct smc_proxy_drvdata *drvdata)
{
	pkvm_handle_t vm_handle;
	struct arm_smccc_res res;
	struct kvm *kvm_dev;
	struct file *kvm_file = drvdata->kvm_file;

	if (!kvm_file)
		return -EINVAL;

	kvm_dev = kvm_file->private_data;
	if (!kvm_dev)
		return -ENODEV;

	vm_handle = kvm_dev->arch.pkvm.handle;
	arm_smccc_1_1_hvc(drvdata->hvc_number, vm_handle, 0, 0, 0, 0, 0, 0, &res);

	return res.a0;
}

static long smc_proxy_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct smc_proxy_drvdata *drvdata =
		container_of(file->private_data, struct smc_proxy_drvdata, misc);
	unsigned long ret = 0L;

	switch (cmd) {
	case SMC_PROXY_SET_VM_TRAP_FORWARD:
		mutex_lock(&drvdata->lock);
		ret = smc_proxy_get_kvm_file_locked(drvdata, arg);
		if (!ret)
			ret = smc_proxy_set_vm_trap_forward_locked(drvdata);
		mutex_unlock(&drvdata->lock);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static const struct file_operations smc_proxy_fops = {
	.release	= smc_proxy_release,
	.unlocked_ioctl = smc_proxy_ioctl,

	.owner = THIS_MODULE,
};

static int __init smc_proxy_probe(struct platform_device *pdev)
{
	struct smc_proxy_drvdata *drvdata;
	struct device *dev = &pdev->dev;
	int ret;

	ret = pkvm_load_el2_module(kvm_nvhe_sym(pkvm_guest_smc_proxy_hyp_init),
				   &pkvm_module_token);
	if (ret) {
		dev_err(dev, "Failed to register pKVM guest SMC proxy: %d\n", ret);
		return ret;
	}

	ret = pkvm_register_el2_mod_call(kvm_nvhe_sym(pkvm_set_guest_smc_trapping_hyp_hvc),
					 pkvm_module_token);
	if (ret < 0)
		return ret;

	drvdata = devm_kmalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	*drvdata = (struct smc_proxy_drvdata) {
		.hvc_number = ret,
		.misc = (struct miscdevice) {
			.parent	= dev,
			.name	= KBUILD_MODNAME,
			.minor	= MISC_DYNAMIC_MINOR,
		        .fops	= &smc_proxy_fops,
			.mode	= SMC_PROXY_MODE,
		},
	};

	mutex_init(&drvdata->lock);

	ret = misc_register(&drvdata->misc);
	if (ret) {
		dev_err(dev, "Failed to register %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, drvdata);
	return ret;
}

static int smc_proxy_remove(struct platform_device *pdev)
{
	struct smc_proxy_drvdata *drvdata = platform_get_drvdata(pdev);
	if (drvdata) {
		misc_deregister(&drvdata->misc);
	}

	return 0;
}

static const struct of_device_id smc_proxy_of_match[] = {
	{ .compatible = "google,smc-proxy" },
	{},
};

static struct platform_driver smc_proxy_driver = {
	.remove = smc_proxy_remove,
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = smc_proxy_of_match,
	},
};

static int __init guest_smc_proxy_init(void)
{
	return platform_driver_probe(&smc_proxy_driver, smc_proxy_probe);
}

module_init(guest_smc_proxy_init);

MODULE_AUTHOR("Sebastian Ene <sebastianene@google.com>");
MODULE_DESCRIPTION("pKVM Guest SMC proxy");
MODULE_LICENSE("GPL v2");
