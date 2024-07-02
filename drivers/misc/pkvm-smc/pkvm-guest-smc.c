// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 - Google Inc
 * Author: Sebastian Ene <sebastianene@google.com>
 * Simple module for pKVM guest SMC handling.
 */
#include <linux/init.h>
#include <linux/file.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <asm/kvm_pkvm_module.h>


#define SMC_PROXY_CHARDEV_NAME		"smc_proxy"
#define SMC_PROXY_SET_VM_TRAP_FORWARD	_IOC(_IOC_WRITE, 'k', 1, 0)
#define SMC_PROXY_MODE			(S_IRUGO | S_IWUGO)

static unsigned long pkvm_module_token;
int kvm_nvhe_sym(pkvm_guest_smc_proxy_hyp_init)(const struct pkvm_module_ops *ops);

void kvm_nvhe_sym(pkvm_set_guest_smc_trapping_hyp_hvc)(struct user_pt_regs *regs);

static int hvc_number;
static int smc_proxy_major;
static struct device *smc_proxy_dev;
static DEFINE_MUTEX(smc_proxy_mtx);

static int smc_proxy_release(struct inode *inode, struct file *file)
{
	mutex_lock(&smc_proxy_mtx);
	if (file->private_data) {
		fput(file->private_data);
		kvm_put_kvm(file->private_data);
	}
	mutex_unlock(&smc_proxy_mtx);
	return 0;
}

static int smc_proxy_get_kvm_file_locked(struct file *file, unsigned int fd)
{
	struct file *kvm_file;

	if (file->private_data)
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

	file->private_data = kvm_file;
	return 0;
}

static int smc_proxy_set_vm_trap_forward_locked(struct file *file)
{
	pkvm_handle_t vm_handle;
	struct arm_smccc_res res;
	struct kvm *kvm_dev;

	if (!file->private_data)
		return -EINVAL;

	kvm_dev = ((struct file *)(file->private_data))->private_data;
	vm_handle = kvm_dev->arch.pkvm.handle;

	arm_smccc_1_1_hvc(hvc_number, vm_handle, 0, 0, 0, 0, 0, 0, &res);
	return res.a0;
}

static long smc_proxy_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned long ret = 0L;

	switch (cmd) {
	case SMC_PROXY_SET_VM_TRAP_FORWARD:
		mutex_lock(&smc_proxy_mtx);
		ret = smc_proxy_get_kvm_file_locked(file, arg);
		if (!ret)
			ret = smc_proxy_set_vm_trap_forward_locked(file);
		mutex_unlock(&smc_proxy_mtx);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static const struct file_operations fops = {
	.open		= simple_open,
	.release	= smc_proxy_release,
	.unlocked_ioctl = smc_proxy_ioctl,

	.owner = THIS_MODULE,
};

static char *smc_proxy_devnode(const struct device *dev, umode_t *mode)
{
	if (mode != NULL)
		*mode = SMC_PROXY_MODE;
	else
		return NULL;

	return kasprintf(GFP_KERNEL, "%s", dev_name(dev));
}

static struct class smc_proxy_class =
{
	.name		= SMC_PROXY_CHARDEV_NAME,
	.devnode	= smc_proxy_devnode,
};

static int __init guest_smc_proxy_init(void)
{
	int ret = 0;

	ret = register_chrdev(0, SMC_PROXY_CHARDEV_NAME, &fops);
	if (ret < 0)
		return ret;

	smc_proxy_major = ret;
	ret = class_register(&smc_proxy_class);
	if (ret)
		goto unregister_dev;

	smc_proxy_dev = device_create(&smc_proxy_class, NULL, MKDEV(smc_proxy_major, 0),
				      NULL, SMC_PROXY_CHARDEV_NAME);
	if (IS_ERR(smc_proxy_dev)) {
		ret = PTR_ERR(smc_proxy_dev);
		goto unregister_class;
	}

	ret = pkvm_load_el2_module(kvm_nvhe_sym(pkvm_guest_smc_proxy_hyp_init),
				   &pkvm_module_token);
	if (ret)
		pr_err("Failed to register pKVM guest SMC proxy: %d\n", ret);
	else
		pr_info("pKVM guest SMC proxy registered successfully with permissive\n");

	ret = pkvm_register_el2_mod_call(kvm_nvhe_sym(pkvm_set_guest_smc_trapping_hyp_hvc),
					 pkvm_module_token);
	if (ret < 0)
		goto clean_device;

	hvc_number = ret;
	return 0;
clean_device:
	device_destroy(&smc_proxy_class, MKDEV(smc_proxy_major, 0));
unregister_class:
	class_unregister(&smc_proxy_class);
unregister_dev:
	unregister_chrdev(smc_proxy_major, SMC_PROXY_CHARDEV_NAME);
	smc_proxy_major = -1;
	return ret;
}

module_init(guest_smc_proxy_init);

MODULE_AUTHOR("Sebastian Ene <sebastianene@google.com>");
MODULE_DESCRIPTION("pKVM Guest SMC proxy");
MODULE_LICENSE("GPL v2");
