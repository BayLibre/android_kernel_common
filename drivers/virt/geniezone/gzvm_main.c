// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 MediaTek Inc.
 */

#include <linux/anon_inodes.h>
#include <linux/device.h>
#include <linux/file.h>
#include <linux/kdev_t.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/gzvm_drv.h>

struct miscdevice *gzvm_debug_dev;

/**
 * gz_err_to_errno() - Convert geniezone return value to standard errno
 *
 * @err: Return value from geniezone function return
 *
 * Return: Standard errno
 */
int gz_err_to_errno(unsigned long err)
{
	int gz_err = (int)err;

	switch (gz_err) {
	case 0:
		return 0;
	case ERR_NO_MEMORY:
		return -ENOMEM;
	case ERR_NOT_SUPPORTED:
		return -EOPNOTSUPP;
	case ERR_NOT_IMPLEMENTED:
		return -EOPNOTSUPP;
	case ERR_FAULT:
		return -EFAULT;
	default:
		break;
	}

	return -EINVAL;
}

/**
 * gzvm_dev_ioctl_check_extension() - Check if given capability is support
 *				      or not
 *
 * @gzvm:
 * @args: Pointer in u64 from userspace
 *
 * Return:
 * * 0			- Support, no error
 * * -EOPNOTSUPP	- Not support
 * * -EFAULT		- Failed to get data from userspace
 */
long gzvm_dev_ioctl_check_extension(struct gzvm *gzvm, unsigned long args)
{
	__u64 cap;
	void __user *argp = (void __user *)args;

	if (copy_from_user(&cap, argp, sizeof(uint64_t)))
		return -EFAULT;
	return gzvm_arch_check_extension(gzvm, cap, argp);
}

static long gzvm_dev_ioctl(struct file *filp, unsigned int cmd,
			   unsigned long user_args)
{
	long ret = -ENOTTY;

	switch (cmd) {
	case GZVM_CREATE_VM:
		ret = gzvm_dev_ioctl_create_vm(user_args);
		break;
	case GZVM_CHECK_EXTENSION:
		if (!user_args)
			return -EINVAL;
		ret = gzvm_dev_ioctl_check_extension(NULL, user_args);
		break;
	default:
		ret = -ENOTTY;
	}

	return ret;
}

static const struct file_operations gzvm_chardev_ops = {
	.unlocked_ioctl = gzvm_dev_ioctl,
	.llseek		= noop_llseek,
};

static struct miscdevice gzvm_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = MODULE_NAME,
	.fops = &gzvm_chardev_ops,
};

static int gzvm_drv_probe(void)
{
	int ret;

	if (gzvm_arch_probe() != 0) {
		pr_err("Not found available conduit\n");
		return -ENODEV;
	}

	ret = misc_register(&gzvm_dev);
	if (ret)
		return ret;
	gzvm_debug_dev = &gzvm_dev;

	return gzvm_drv_irqfd_init();
}

static int gzvm_drv_remove(void)
{
	gzvm_drv_irqfd_exit();
	destroy_all_vm();
	misc_deregister(&gzvm_dev);
	return 0;
}

static int gzvm_dev_init(void)
{
	return gzvm_drv_probe();
}

static void gzvm_dev_exit(void)
{
	gzvm_drv_remove();
}

module_init(gzvm_dev_init);
module_exit(gzvm_dev_exit);

MODULE_AUTHOR("MediaTek");
MODULE_DESCRIPTION("GenieZone interface for VMM");
MODULE_LICENSE("GPL");
