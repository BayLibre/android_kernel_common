// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2026 Google.
 *
 */

#define pr_fmt(fmt)     "DMAR: pkvm: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include "iommu.h"

int __init pkvm_host_prepare_iommu(void)
{
	return 0;
}

int __init pkvm_host_init_iommu(void)
{
	int ret = intel_iommu_init();

	if (!ret)
		pr_info("pvIOMMU initialized!\n");
	else
		pr_err("pvIOMMU initialize failed(err=%d)\n", ret);

	return ret;
}
