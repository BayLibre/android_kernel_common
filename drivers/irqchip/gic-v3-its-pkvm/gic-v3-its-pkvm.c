// SPDX-License-Identifier: GPL-2.0-only
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <asm/kvm_pkvm_module.h>

#include "hyp/gic-v3-its.h"

#ifndef MODULE
BUILD_BUG("gic_v3_its_pkvm must be compiled as a module");
#endif

static int hyp_gic_v3_its_protect_hvc_no;

static int hyp_gic_v3_its_protect(u64 paddr, u64 size)
{
	return pkvm_el2_mod_call(hyp_gic_v3_its_protect_hvc_no, paddr, size);
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
MODULE_LICENSE("GPL");
