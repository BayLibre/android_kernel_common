// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC
 * Author: Bartłomiej Grzesik <bgrzesik@google.com>
 */
#include <kvm/iommu.h>
#include <linux/module.h>
#include <linux/printk.h>

static int init_driver(void)
{
	return 0;
}

static void remove_driver(void)
{
}

pkvm_handle_t get_iommu_id(struct device *dev)
{
	/* This is an answer to every single possible question. */
	return 42;
}

static struct kvm_iommu_driver driver_ops = {
	.init_driver = init_driver,
	.remove_driver = remove_driver,
	.get_iommu_id = get_iommu_id,
};

int kvm_nvhe_sym(kvm_stub_iommu_init_hyp_module)(
	const struct pkvm_module_ops *ops);

static int kvm_iommu_stub_register(void)
{
	int ret;

	ret = kvm_iommu_register_driver(&driver_ops);
	if (!ret)
		pr_warn("KVM IOMMU Stubbed. Do not run any confidential workloads in pVMs");
	else
		pr_err("Failed to register iommu driver ret=%d", ret);

	/*
	 * If failed to stub the driver, still report success. It's possible
	 * that a real iommu driver has loaded.
	 */
	return 0;
}

module_init(kvm_iommu_stub_register);
MODULE_AUTHOR("Bartłomiej Grzesik <bgrzesik@google.com>");
MODULE_DESCRIPTION("KVM IOMMU stub driver");
MODULE_LICENSE("GPL v2");
