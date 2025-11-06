// SPDX-License-Identifier: GPL-2.0-only
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/irqchip/arm-gic-v3.h>

#include <asm/kvm_pkvm_module.h>

#include "hyp/gic-v3-its.h"

#ifndef MODULE
BUILD_BUG("gic_v3_its_pkvm must be compiled as a module");
#endif

static int hyp_gic_v3_its_protect_hvc_no;
static int hyp_gic_v3_redist_protect_hvc_no;

static int hyp_gic_v3_its_protect(u64 paddr, u64 size)
{
	return pkvm_el2_mod_call(hyp_gic_v3_its_protect_hvc_no, paddr, size);
}

static int hyp_gic_v3_redist_protect(u64 paddr, u64 size, u64 stride)
{
	return pkvm_el2_mod_call(hyp_gic_v3_redist_protect_hvc_no, paddr, size,
				 stride);
}

static int protect_gicr(void)
{
	struct device_node *np = NULL;
	struct resource res;
	u64 stride, nr_regions;
	int i, ret;

	np = of_find_compatible_node(NULL, NULL, "arm,gic-v3");
	if (!np) {
		pr_err("Could not find GIC DT node, despite ITS being present\n");
		return -ENODEV;
	}

	if (of_property_read_u64(np, "redistributor-stride", &stride))
		stride = 0;

	if (of_property_read_u64(np, "#redistributor-regions", &nr_regions))
		nr_regions = 1;

	for (i = 1; i < nr_regions + 1; i++) {
		ret = of_address_to_resource(np, 1, &res);
		if (ret) {
			pr_err("Failed to get address: %d\n", ret);
			return ret;
		}

		/* TODO: Pack everything into one allocation and send to hyp in a single hcall */
		ret = hyp_gic_v3_redist_protect(res.start, resource_size(&res),
						stride);
		if (ret) {
			pr_err("Hypervisor failed to register GICR: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int __init gic_v3_its_pkvm_init(void)
{
	struct device_node *np = NULL;
	struct resource res;
	size_t count = 0;
	int ret = 0;

	if (!is_protected_kvm_enabled())
		return 0;


	for_each_compatible_node(np, NULL, "arm,gic-v3-its")
		count++;

	if (count == 0) {
		pr_warn("GIC V3 ITS was not found");
		return 0;
	}

	ret = pkvm_load_el2_module(hyp_gic_v3_its_init);
	if (ret) {
		pr_err("Failed to load GIC V3 ITS EL2 module: %d\n", ret);
		return ret;
	}

	ret = pkvm_register_el2_mod_call(hyp_gic_v3_its_protect_hvc);
	if (ret < 0) {
		pr_err("Failed to register HVC: %d\n", ret);
		return ret;
	}

	hyp_gic_v3_its_protect_hvc_no = ret;

	ret = pkvm_register_el2_mod_call(hyp_gic_v3_redist_protect_hvc);
	if (ret < 0) {
		pr_err("Failed to register HVC: %d\n", ret);
		return ret;
	}

	hyp_gic_v3_redist_protect_hvc_no = ret;

	ret = protect_gicr();
	if (ret) {
		pr_err("Failed to protect GICR: %d\n", ret);
		return ret;
	}

	for_each_compatible_node(np, NULL, "arm,gic-v3-its") {
		ret = of_address_to_resource(np, 0, &res);
		if (ret) {
			pr_err("Failed to get address: %d\n", ret);
			return ret;
		}

		ret = hyp_gic_v3_its_protect(res.start, resource_size(&res));
		if (ret) {
			pr_err("Hypervisor failed to register ITS: %d\n", ret);
			return ret;
		}
	}

	pr_info("GIC V3 ITS pKVM trap and emulate initialized");

	return ret;
}

#ifdef MODULE
module_init(gic_v3_its_pkvm_init);
#else
core_initcall(gic_v3_its_pkvm_init);
#endif

MODULE_DESCRIPTION("pKVM GIC/ITS trap and emulate driver.");
MODULE_LICENSE("GPL v2");
