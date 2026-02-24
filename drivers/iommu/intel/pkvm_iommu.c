// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2026 Google.
 */

#define pr_fmt(fmt)     "DMAR: pkvm: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/iommu.h>
#include <asm/kvm_host.h>
#include "iommu.h"

int __init pkvm_host_prepare_iommu(void)
{
	return 0;
}

/*
 * NOTE: Host driver does cleanup if initialization fails, but
 * doesn't undo all initialization steps. We undo those steps
 * missed by host driver.
 */
static void __init pkvm_host_undo_iommu(void)
{
	struct dmar_drhd_unit *drhd;
	struct intel_iommu *iommu;

	BUG_ON(pkvm_enabled());

	/*
	 * Hypervisor might have initialized QI and host
	 * state of QI would be in an inconsistent state.
	 * Re-enable QI to start from a clean state.
	 */
	down_write(&dmar_global_lock);
	for_each_iommu(iommu, drhd) {
		if (iommu->qi)
			dmar_reenable_qi(iommu);
	}
	up_write(&dmar_global_lock);

	bus_unregister_notifier(&pci_bus_type, &dmar_pci_bus_nb);
#ifdef CONFIG_IOMMU_DEBUGFS
	debugfs_lookup_and_remove("intel", iommu_debugfs_dir);
#endif
}

int __init pkvm_host_init_iommu(void)
{
	static int ret;

	/*
	 * We might get called a second time if iommu initialization
	 * failed while deprivileged. In that case, undo the partial
	 * iommu initialization before trying the second time (this
	 * time we are reprivileged and pKVM disabled).
	 */
	if (ret) {
		pkvm_host_undo_iommu();
		pr_info("Retrying IOMMU initialization after reprivilege!\n");
	}

	ret = intel_iommu_init();

	if (!ret)
		pr_info("IOMMU initialized!\n");
	else
		pr_err("IOMMU initialize failed(err=%d)\n", ret);

	return ret;
}
