// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>

struct pkvm_pm_domain {
	struct genpd_onecell_data onecell;
	struct platform_device *pdev;
};

#define to_pkvm_pd(gpd) container_of(gpd, struct pkvm_pm_domain, genpd)

static int pkvm_pm_domain_power(struct generic_pm_domain *domain, bool power_on)
{
	dev_info(&domain->dev, "pkvm_pm_domain_power(%d)\n", power_on);
	// TODO: SMC

	return 0;
}

static int pkvm_pm_domain_power_on(struct generic_pm_domain *domain)
{
	return pkvm_pm_domain_power(domain, true);
}

static int pkvm_pm_domain_power_off(struct generic_pm_domain *domain)
{
	return pkvm_pm_domain_power(domain,  false);
}

static int pkvm_pm_domain_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pkvm_pm_domain *pd;
	struct generic_pm_domain **domains;
	struct generic_pm_domain *domain;
	u32 num_domains;
	int ret;

	pd = devm_kzalloc(dev, sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return -ENOMEM;

	pd->pdev = pdev;

	ret = of_property_read_u32(dev->of_node, "num-domains", &num_domains);
	if (ret)
		return ret;
	dev_info(dev, "pkvm num_domains = %u\n", num_domains);

	domains = devm_kcalloc(dev, (size_t)num_domains, sizeof(*domains), GFP_KERNEL);
	if (!domains)
		return -ENOMEM;

	for (size_t i = 0; i < (size_t)num_domains; i++) {
		domain = devm_kzalloc(dev, sizeof(*domain), GFP_KERNEL);
		if (!domain)
			return -ENOMEM;
	}

	for (size_t i = 0; i < (size_t)num_domains; i++) {
		domains[i]->name = "pkvm-genpd";
		domains[i]->power_off = pkvm_pm_domain_power_off;
		domains[i]->power_on = pkvm_pm_domain_power_on;
		domains[i]->flags = GENPD_FLAG_DEV_NAME_FW;

		ret = pm_genpd_init(domains[i], NULL, false);
		if (ret)
			return ret; // TODO: clean up?
	}

	pd->onecell.domains = domains;
	pd->onecell.num_domains = (unsigned int)num_domains;

	dev_set_drvdata(dev, pd);

	/* ret = of_genpd_add_provider_simple(dev->of_node, &pd->genpd); */
	/* dev_info(dev, "add_provider_simple() = %d\n", ret); */

	ret = of_genpd_add_provider_onecell(dev->of_node, &pd->onecell);
	dev_info(dev, "add_provider_onecell() = %d\n", ret);

	return ret;
}

static void pkvm_pm_domain_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pkvm_pm_domain *pd = dev_get_drvdata(dev);

	of_genpd_del_provider(dev->of_node);

	if (!pd)
		return;

	for (size_t i = 0; i < pd->onecell.num_domains; i++)
		pm_genpd_remove(pd->onecell.domains[i]);
}

static const struct of_device_id pkvm_pm_domain_of_match[] = {
	{ .compatible = "pkvm,power-controller" },
	{}
};
MODULE_DEVICE_TABLE(of, pkvm_pm_domain_of_match);

static struct platform_driver pkvm_pm_domain_driver = {
	.probe = pkvm_pm_domain_probe,
	.remove_new = pkvm_pm_domain_remove,
	.driver		= {
		.name	= "pkvm-power-controller",
		.of_match_table = pkvm_pm_domain_of_match,
	},
};

static int __init pkvm_pm_domain_init_driver(void)
{
	return platform_driver_register(&pkvm_pm_domain_driver);
}
subsys_initcall(pkvm_pm_domain_init_driver);

static void __exit pkvm_pm_domain_exit_driver(void)
{
	platform_driver_unregister(&pkvm_pm_domain_driver);
}
module_exit(pkvm_pm_domain_exit_driver);

MODULE_AUTHOR("TODO");
MODULE_DESCRIPTION("TODO");
MODULE_LICENSE("GPL v2");
