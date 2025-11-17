// SPDX-License-Identifier: GPL-2.0
/*
 * pKVM host driver for the Arm SMMUv3
 *
 * Copyright (C) 2022 Linaro Ltd.
 */
#include <asm/kvm_mmu.h>
#include <asm/kvm_pkvm.h>

#include <linux/auxiliary_bus.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include "arm-smmu-v3.h"
#include "pkvm/arm_smmu_v3_nested.h"

extern struct kvm_iommu_ops kvm_nvhe_sym(smmu_ops);

/*
 * Pre allocated pages that can be used from the EL2 part of the driver from atomic
 * context, ideally used for page table pages for identity domains.
 */
static int atomic_pages;
module_param(atomic_pages, int, 0);

static struct platform_driver smmuv3_nesting_driver;
static int smmuv3_nesting_probe(struct platform_device *pdev)
{
	return 0;
}

static int smmu_alloc_atomic_mc(struct kvm_hyp_memcache *atomic_mc)
{
	int ret;
#ifndef MODULE
	u64 i;
	phys_addr_t start, end;

	/*
	 * Allocate pages to cover mapping with PAGE_SIZE for all memory
	 * Then allocate extra for 1GB of MMIO.
	 * Add 10 extra pages as we map the rest with first level blocks
	 * for PAGE_SIZE = 4KB, that should cover 5TB of address space.
	 */
	for_each_mem_range(i, &start, &end) {
		atomic_pages += __hyp_pgtable_max_pages((end - start) >> PAGE_SHIFT);
	}

	atomic_pages += __hyp_pgtable_max_pages(SZ_1G >> PAGE_SHIFT) + 10;
#endif

	/* Module didn't set that parameter. */
	if (!atomic_pages)
		return 0;

	/* For PGD*/
	ret = topup_hyp_memcache(atomic_mc, 1, 3);
	if (ret)
		return ret;
	ret = topup_hyp_memcache(atomic_mc, atomic_pages, 0);
	if (ret)
		return ret;
	pr_info("smmuv3: Allocated %d MiB for atomic usage\n",
		(atomic_pages << PAGE_SHIFT) / SZ_1M);
	/* Topup hyp alloc so IOMMU driver can allocate domains. */
	__pkvm_topup_hyp_alloc(1);

	return ret;
}

static int kvm_arm_smmu_v3_post_init(void)
{
	if (!is_protected_kvm_enabled() || !kvm_arm_smmu_cur)
		return 0;

	platform_driver_unregister(&smmuv3_nesting_driver);
	(void)bus_rescan_devices(&platform_bus_type);
	return 0;
}

static pkvm_handle_t kvm_arm_v3_id_by_of(struct device_node *np)
{
	return 0;
}

static int kvm_arm_smmu_v3_init_drv(void)
{
	struct kvm_hyp_memcache atomic_mc;
	int ret;

	ret = platform_driver_probe(&smmuv3_nesting_driver, smmuv3_nesting_probe);
	if (ret)
		return ret;

#ifdef MODULE
	/* TBD: pkvm_load_el2_module */
#endif

	init_hyp_memcache(&atomic_mc);

	ret = smmu_alloc_atomic_mc(&atomic_mc);
	if (ret)
		return ret;

	ret = kvm_iommu_init_hyp(kern_hyp_va(lm_alias(&kvm_nvhe_sym(smmu_ops))), &atomic_mc);
	if (ret)
		return ret;

	return kvm_arm_smmu_v3_post_init();
}

static struct kvm_iommu_driver kvm_smmu_v3_ops = {
	.init_driver = kvm_arm_smmu_v3_init_drv,
	.get_iommu_id_by_of = kvm_arm_v3_id_by_of,
};

static int kvm_arm_smmu_v3_register(void)
{
	if (!is_protected_kvm_enabled())
		return 0;

	return kvm_iommu_register_driver(&kvm_smmu_v3_ops);
};

static int smmu_create_aux_device(struct device *dev, void *data)
{
	static int dev_id;
	struct auxiliary_device *auxdev;

	auxdev = __devm_auxiliary_device_create(dev, "protected_kvm",
						"smmu_v3_emu", NULL, dev_id++);
	if (!auxdev)
		return -ENODEV;

	auxdev->dev.parent = dev;

	return 0;
}

static struct platform_driver smmuv3_nesting_driver;

static const struct of_device_id smmuv3_nested_of_match[] = {
	{ .compatible = "arm,smmu-v3", },
	{ },
};

static struct platform_driver smmuv3_nesting_driver = {
	.driver = {
		.name = "smmuv3-nesting",
		.of_match_table = smmuv3_nested_of_match,
	},
};
subsys_initcall(kvm_arm_smmu_v3_register);
