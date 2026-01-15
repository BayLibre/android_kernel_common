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
	struct dmar_drhd_unit *drhd;
	struct intel_iommu *iommu;
	int ret;

	down_write(&dmar_global_lock);
	ret = dmar_table_init();
	if (ret) {
		pr_err("Failed to initialize DMAR table!\n");
		goto out;
	}

	ret = -ENODEV;
	for_each_iommu(iommu, drhd) {
		if (drhd->ignored) {
			pr_warn("iommu%d ignored, but pKVM needs iommu to be enabled!\n",
				iommu->seq_id);
			goto out;
		}

		if (readl(iommu->reg + DMAR_GSTS_REG) & DMA_GSTS_TES) {
			pr_warn("iommu%d: Translation enabled before initialization!\n",
					iommu->seq_id);
			goto out;
		}

		/*
		 * Since pKVM is not expected to be supported on ancient hardware which
		 * requires write buffer flushing, require cap_rwbf=0 for simplicity.
		 */
		if (cap_rwbf(iommu->cap)) {
			pr_warn("iommu%d: CAP.RWBF=1 is not supported!\n", iommu->seq_id);
			goto out;
		}

		/* pKVM expects Queued Invalidation support for simplicity and efficiency */
		if (!ecap_qis(iommu->ecap)) {
			pr_warn("iommu%d: queued Invalidation not supported!\n", iommu->seq_id);
			goto out;
		}
	}

	pkvm_sym(intel_iommu_sm) = intel_iommu_sm;

	for_each_iommu(iommu, drhd) {
		struct intel_iommu_info info = {
			.reg_phys = iommu->reg_phys,
			.reg_size = iommu->reg_size,
			.cap = iommu->cap,
			.ecap = iommu->ecap,
			.seq_id = iommu->seq_id,
			.agaw = iommu->agaw,
			.msagaw = iommu->msagaw,
		};

		ret = pkvm_sym(prepare_iommu)(&info);
		if (ret)
			goto out;
	}
out:
	up_write(&dmar_global_lock);
	return ret;
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
