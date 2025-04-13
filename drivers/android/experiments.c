// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * experiments.c - Kernel Experiments module
 *
 * Copyright (C) 2025 Deepa Dinamani <deepadinamani@google.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "exp_attr.h"

#include <linux/kernfs.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/sysfs.h>

static void exp_release(struct kobject *kobj)
{
	kfree(kobj);
}

static ssize_t exp_show(struct kobject *kobj, struct attribute *attr,
			char *buf)
{
	struct exp_attr *exp = container_of(attr, struct exp_attr, attr);
	return sysfs_emit(buf, "%d\n", exp->enable);
}

static void exp_enable(struct exp_attr *exp)
{
	if (!exp->enable) {
		exp->enable = 1;
		tracepoint_probe_register(exp->tp, exp->probe_fn, exp);
	}
}

static void exp_disable(struct exp_attr *exp)
{
	if (exp->enable) {
		exp->enable = 0;
		tracepoint_probe_unregister(exp->tp, exp->probe_fn, exp);
	}
}

static ssize_t exp_store(struct kobject *kobj, struct attribute *attr,
			 const char *buf, size_t count)
{
	struct exp_attr *exp = container_of(attr, struct exp_attr, attr);
	int ret, val;

	ret = kstrtoint(buf, 10, &val);
	if (ret < 0)
		return ret;

	if (val == 1) {
		exp_enable(exp);
		ret = count;
	} else if (val == 0) {
		exp_disable(exp);
		ret = count;
	}
	else
		ret = -EINVAL;

	return ret;
}

static const struct sysfs_ops exp_ops = {
	.show = exp_show,
	.store = exp_store,
};

static const struct kobj_type exp_ktype = {
	.release = exp_release,
	.sysfs_ops = &exp_ops,
};

static struct kobject *experiments_kobj;

static int experiments_init(void)
{
	int ret, i;

	experiments_kobj = (struct kobject *)kzalloc(sizeof(struct kobject), GFP_KERNEL);
	if (!experiments_kobj)
		return -ENOMEM;

	ret = kobject_init_and_add(experiments_kobj, &exp_ktype, kernel_kobj, "%s", "experiments");
	if (ret) {
		kobject_put(experiments_kobj);
		return ret;
	}

	for(i = 0; experiment_count; i++) {
		ret = sysfs_create_file(experiments_kobj, &experiments[i].attr);
		if (ret)
			return ret;

		printk(KERN_INFO pr_fmt("Initialized android kernel experiment %s.\n"), experiments[i].attr.name);
	}

	return ret;
}

static void experiments_exit(void)
{
	int i;
	for(i = 0; i < experiment_count; i++) {
		exp_disable(&experiments[i]);
	}
	kobject_put(experiments_kobj);
}

module_init(experiments_init);
module_exit(experiments_exit);
MODULE_LICENSE("GPL");
MODULE_INFO(experiments, "Y");
