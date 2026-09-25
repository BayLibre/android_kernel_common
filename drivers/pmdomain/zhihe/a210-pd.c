// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Zhihe Computing Limited.
 */

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_clk.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <asm/sbi.h>

#define A210_PCA_CTRL			0x20

#define A210_BPC_MODE			0x00
#define A210_BPC_FENCE			0x04

#define A210_PCU_LPREQ			0x08
#define A210_PCU_LPSTATE		0x0c
#define A210_PCU_INT_EN			0x24
#define A210_PCU_INT_CLR		0x28
#define A210_PCU_INT_STS		0x2c
#define A210_PCU_INT_ALL		GENMASK(5, 0)
#define A210_PCU_INT_DENY		(BIT(1) | BIT(4))
#define A210_PCU_INT_TIMEOUT		(BIT(2) | BIT(5))
#define A210_PCU_TIMEOUT_US		10000

#define A210_PD_STATE_OFF		0x00
#define A210_PD_STATE_ON		0x1f

/* Zhihe OpenSBI vendor extension, outside the SBI vendor extension space */
#define SBI_EXT_ZH_IOPMP		0x5A5A5A5A
#define SBI_EXT_ZH_IOPMP_ADD_RULE	0
#define SBI_EXT_ZH_IOPMP_REMOVE_RULE	1
/* The firmware takes at most this many device ids per call (a1..a5). */
#define SBI_EXT_ZH_IOPMP_MAX_IDS	5

#define A210_PD_MAX_IOPMPS		16

struct a210_pd_soc {
	struct device *dev;
	bool has_iopmp;
};

struct a210_pm_domain {
	struct generic_pm_domain pd;
	struct a210_pd_soc *soc;
	struct device_node *np;
	struct regulator *regulator;
	struct reset_control *reset;
	struct clk_bulk_data *clks;
	int num_clks;
	u32 device_ids[A210_PD_MAX_IOPMPS];
	u32 num_device_ids;
	void __iomem *pca_base;
	void __iomem *bpc_base;
	void __iomem *pcu_base;
};

static inline struct a210_pm_domain *to_a210_pd(struct generic_pm_domain *domain)
{
	return container_of(domain, struct a210_pm_domain, pd);
}

static int a210_pd_pcu_request(struct a210_pm_domain *a210_pd, u32 state)
{
	struct device *dev = a210_pd->soc->dev;
	void __iomem *base = a210_pd->pcu_base;
	u32 sts;
	int ret;

	writel(A210_PCU_INT_ALL, base + A210_PCU_INT_EN);
	writel(state, base + A210_PCU_LPSTATE);
	writel(0x1, base + A210_PCU_LPREQ);

	ret = readl_poll_timeout(base + A210_PCU_INT_STS, sts, sts, 1,
				 A210_PCU_TIMEOUT_US);
	if (ret) {
		dev_err(dev, "%s: no answer from the PCU\n", a210_pd->pd.name);
		return ret;
	}

	if (sts & A210_PCU_INT_DENY)
		dev_err(dev, "%s: PCU request denied\n", a210_pd->pd.name);
	if (sts & A210_PCU_INT_TIMEOUT)
		dev_err(dev, "%s: PCU request timed out\n", a210_pd->pd.name);

	writel(sts, base + A210_PCU_INT_CLR);

	return 0;
}

static int a210_pd_power_switch(struct a210_pm_domain *a210_pd, bool on)
{
	int ret;

	if (on && a210_pd->regulator) {
		ret = regulator_enable(a210_pd->regulator);
		if (ret)
			return ret;
	}

	if (on && a210_pd->pca_base)
		writel(0x0, a210_pd->pca_base + A210_PCA_CTRL);

	if (a210_pd->bpc_base) {
		/* hardware controlled mode */
		writel(0x0, a210_pd->bpc_base + A210_BPC_MODE);
		writel(0x10101, a210_pd->bpc_base + A210_BPC_FENCE);
	}

	if (a210_pd->pcu_base) {
		ret = a210_pd_pcu_request(a210_pd, on ? A210_PD_STATE_ON :
							A210_PD_STATE_OFF);
		if (ret) {
			if (on && a210_pd->regulator)
				regulator_disable(a210_pd->regulator);
			return ret;
		}
	}

	if (!on && a210_pd->regulator)
		return regulator_disable(a210_pd->regulator);

	return 0;
}

/*
 * Each subsystem's DMA masters sit behind a DFMU IOPMP bus firewall that
 * denies all accesses until firmware applies the rules of the devices
 * behind it, which are lost again when the domain is switched off.
 */
static int a210_pd_iopmp_ctrl(struct a210_pm_domain *a210_pd, int fid)
{
	unsigned long ids[SBI_EXT_ZH_IOPMP_MAX_IDS];
	struct sbiret ret;
	u32 i, j, n;

	if (!a210_pd->soc->has_iopmp)
		return 0;

	for (i = 0; i < a210_pd->num_device_ids; i += n) {
		n = min_t(u32, a210_pd->num_device_ids - i,
			  SBI_EXT_ZH_IOPMP_MAX_IDS);

		memset(ids, 0, sizeof(ids));
		for (j = 0; j < n; j++)
			ids[j] = a210_pd->device_ids[i + j];

		ret = sbi_ecall(SBI_EXT_ZH_IOPMP, fid, n,
				ids[0], ids[1], ids[2], ids[3], ids[4]);
		if (ret.error)
			return sbi_err_map_linux_errno(ret.error);
	}

	return 0;
}

static int a210_pd_on(struct a210_pm_domain *a210_pd)
{
	struct device *dev = a210_pd->soc->dev;
	int ret;

	ret = a210_pd_power_switch(a210_pd, true);
	if (ret)
		return ret;

	ret = clk_bulk_prepare_enable(a210_pd->num_clks, a210_pd->clks);
	if (ret)
		goto err_power_off;

	ret = reset_control_deassert(a210_pd->reset);
	if (ret)
		goto err_disable_clks;

	ret = a210_pd_iopmp_ctrl(a210_pd, SBI_EXT_ZH_IOPMP_ADD_RULE);
	if (ret) {
		dev_err(dev, "%s: failed to apply iopmp rules: %d\n",
			a210_pd->pd.name, ret);
		goto err_assert_reset;
	}

	return 0;

err_assert_reset:
	reset_control_assert(a210_pd->reset);
err_disable_clks:
	clk_bulk_disable_unprepare(a210_pd->num_clks, a210_pd->clks);
err_power_off:
	a210_pd_power_switch(a210_pd, false);
	return ret;
}

static int a210_pd_power_on(struct generic_pm_domain *domain)
{
	return a210_pd_on(to_a210_pd(domain));
}

static int a210_pd_power_off(struct generic_pm_domain *domain)
{
	struct a210_pm_domain *a210_pd = to_a210_pd(domain);
	int ret;

	ret = a210_pd_iopmp_ctrl(a210_pd, SBI_EXT_ZH_IOPMP_REMOVE_RULE);
	if (ret) {
		dev_err(a210_pd->soc->dev, "%s: failed to drop iopmp rules: %d\n",
			domain->name, ret);
		return ret;
	}

	clk_bulk_disable_unprepare(a210_pd->num_clks, a210_pd->clks);

	ret = reset_control_assert(a210_pd->reset);
	if (ret)
		return ret;

	return a210_pd_power_switch(a210_pd, false);
}

static void __iomem *a210_pd_iomap(struct device *dev, struct device_node *np,
				   const char *name)
{
	int idx = of_property_match_string(np, "reg-names", name);

	if (idx < 0)
		return NULL;

	return devm_of_iomap(dev, np, idx, NULL);
}

static void a210_pd_reset_put(void *data)
{
	reset_control_put(data);
}

static void a210_pd_clks_put(void *data)
{
	struct a210_pm_domain *a210_pd = data;

	clk_bulk_put(a210_pd->num_clks, a210_pd->clks);
}

static int a210_pd_get_clks(struct a210_pm_domain *a210_pd)
{
	struct device *dev = a210_pd->soc->dev;
	struct device_node *np = a210_pd->np;
	int num_clks = of_clk_get_parent_count(np);
	int i, ret;

	if (!num_clks)
		return 0;

	a210_pd->clks = devm_kcalloc(dev, num_clks, sizeof(*a210_pd->clks),
				     GFP_KERNEL);
	if (!a210_pd->clks)
		return -ENOMEM;

	for (i = 0; i < num_clks; i++) {
		a210_pd->clks[i].clk = of_clk_get(np, i);
		if (IS_ERR(a210_pd->clks[i].clk)) {
			ret = PTR_ERR(a210_pd->clks[i].clk);
			clk_bulk_put(i, a210_pd->clks);
			return dev_err_probe(dev, ret, "%pOFn: failed to get clock %d\n",
					     np, i);
		}
	}
	a210_pd->num_clks = num_clks;

	return devm_add_action_or_reset(dev, a210_pd_clks_put, a210_pd);
}

static int a210_pd_get_regulator(struct a210_pm_domain *a210_pd)
{
	struct device *dev = a210_pd->soc->dev;
	struct device_node *np = a210_pd->np;
	struct device_node *supply_np;
	u32 max_uV;
	int ret;

	if (!of_property_present(np, "pmic-supply"))
		return 0;

	a210_pd->regulator = devm_of_regulator_get_optional(dev, np, "pmic");
	if (IS_ERR(a210_pd->regulator))
		return dev_err_probe(dev, PTR_ERR(a210_pd->regulator),
				     "%pOFn: failed to get pmic supply\n", np);

	supply_np = of_parse_phandle(np, "pmic-supply", 0);
	ret = of_property_read_u32(supply_np, "regulator-max-microvolt", &max_uV);
	of_node_put(supply_np);
	if (ret)
		return 0;

	ret = regulator_set_voltage(a210_pd->regulator, max_uV, max_uV);
	if (ret)
		return dev_err_probe(dev, ret, "%pOFn: failed to set pmic voltage\n", np);

	return 0;
}

/* Acquire everything a domain needs, without touching the hardware yet. */
static struct a210_pm_domain *a210_pd_get(struct a210_pd_soc *pd_soc,
					  struct device_node *np)
{
	struct device *dev = pd_soc->dev;
	struct a210_pm_domain *a210_pd;
	int count, i, ret;

	a210_pd = devm_kzalloc(dev, sizeof(*a210_pd), GFP_KERNEL);
	if (!a210_pd)
		return ERR_PTR(-ENOMEM);

	a210_pd->soc = pd_soc;
	a210_pd->np = np;
	a210_pd->pd.name = np->name;
	a210_pd->pd.power_on = a210_pd_power_on;
	a210_pd->pd.power_off = a210_pd_power_off;

	a210_pd->pca_base = a210_pd_iomap(dev, np, "pca");
	a210_pd->bpc_base = a210_pd_iomap(dev, np, "bpc");
	a210_pd->pcu_base = a210_pd_iomap(dev, np, "pcu");
	if (IS_ERR(a210_pd->pca_base))
		return ERR_CAST(a210_pd->pca_base);
	if (IS_ERR(a210_pd->bpc_base))
		return ERR_CAST(a210_pd->bpc_base);
	if (IS_ERR(a210_pd->pcu_base))
		return ERR_CAST(a210_pd->pcu_base);

	ret = a210_pd_get_regulator(a210_pd);
	if (ret)
		return ERR_PTR(ret);

	a210_pd->reset = of_reset_control_array_get_optional_shared(np);
	if (IS_ERR(a210_pd->reset))
		return ERR_PTR(dev_err_probe(dev, PTR_ERR(a210_pd->reset),
					     "%pOFn: failed to get resets\n", np));
	ret = devm_add_action_or_reset(dev, a210_pd_reset_put, a210_pd->reset);
	if (ret)
		return ERR_PTR(ret);

	ret = a210_pd_get_clks(a210_pd);
	if (ret)
		return ERR_PTR(ret);

	count = of_count_phandle_with_args(np, "iopmps", NULL);
	if (count > A210_PD_MAX_IOPMPS) {
		dev_err(dev, "%pOFn: too many iopmps (%d)\n", np, count);
		return ERR_PTR(-EINVAL);
	}
	for (i = 0; i < count; i++) {
		struct device_node *iopmp_np = of_parse_phandle(np, "iopmps", i);
		u32 device_id;

		if (!iopmp_np)
			return ERR_PTR(-EINVAL);

		if (!of_property_read_u32(iopmp_np, "device-id", &device_id))
			a210_pd->device_ids[a210_pd->num_device_ids++] = device_id;
		of_node_put(iopmp_np);
	}

	return a210_pd;
}

static int a210_pd_add_subdomains(struct device *dev)
{
	struct of_phandle_args child_args, parent_args;
	int ret;

	for_each_available_child_of_node_scoped(dev->of_node, child) {
		if (of_parse_phandle_with_args(child, "power-domains",
					       "#power-domain-cells", 0,
					       &parent_args))
			continue;

		child_args.np = child;
		child_args.args_count = 0;

		ret = of_genpd_add_subdomain(&parent_args, &child_args);
		of_node_put(parent_args.np);
		if (ret)
			return dev_err_probe(dev, ret, "%pOFn: failed to add subdomain\n",
					     child);
	}

	return 0;
}

static int a210_pd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct a210_pm_domain **domains;
	struct a210_pd_soc *pd_soc;
	int num_domains = 0;
	int i, ret;

	pd_soc = devm_kzalloc(dev, sizeof(*pd_soc), GFP_KERNEL);
	if (!pd_soc)
		return -ENOMEM;
	pd_soc->dev = dev;

	pd_soc->has_iopmp = sbi_probe_extension(SBI_EXT_ZH_IOPMP) > 0;
	if (!pd_soc->has_iopmp)
		dev_info(dev, "no firmware IOPMP extension, not managing iopmp rules\n");

	domains = devm_kcalloc(dev, of_get_available_child_count(dev->of_node),
			       sizeof(*domains), GFP_KERNEL);
	if (!domains)
		return -ENOMEM;

	/*
	 * Get every domain's resources first: nothing is registered or powered
	 * until all of them are available, so an -EPROBE_DEFER needs no unwind.
	 */
	for_each_available_child_of_node_scoped(dev->of_node, child) {
		struct a210_pm_domain *a210_pd = a210_pd_get(pd_soc, child);

		if (IS_ERR(a210_pd))
			return PTR_ERR(a210_pd);
		domains[num_domains++] = a210_pd;
	}

	for (i = 0; i < num_domains; i++) {
		struct a210_pm_domain *a210_pd = domains[i];
		bool is_off = false;

		/*
		 * Power every domain on, so that its state is known and genpd
		 * keeps it on until all its consumers have probed.
		 */
		ret = a210_pd_on(a210_pd);
		if (ret) {
			dev_warn(dev, "%s: failed to power on: %d\n",
				 a210_pd->pd.name, ret);
			is_off = true;
		}

		ret = pm_genpd_init(&a210_pd->pd, NULL, is_off);
		if (ret)
			goto err_remove;

		ret = of_genpd_add_provider_simple(a210_pd->np, &a210_pd->pd);
		if (ret) {
			pm_genpd_remove(&a210_pd->pd);
			goto err_remove;
		}
	}

	ret = a210_pd_add_subdomains(dev);
	if (ret)
		goto err_remove;

	return 0;

err_remove:
	while (--i >= 0) {
		of_genpd_del_provider(domains[i]->np);
		pm_genpd_remove(&domains[i]->pd);
	}
	return ret;
}

static const struct of_device_id a210_pd_of_match[] = {
	{ .compatible = "zhihe,a210-power-domain" },
	{ /* Sentinel */ },
};
MODULE_DEVICE_TABLE(of, a210_pd_of_match);

static struct platform_driver a210_pd_driver = {
	.probe = a210_pd_probe,
	.driver = {
		.name = "a210-power-domain",
		.of_match_table = a210_pd_of_match,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver(a210_pd_driver);

MODULE_AUTHOR("dong.yan <yand@zhcomputing.com>");
MODULE_DESCRIPTION("Zhihe A210 power domain driver");
MODULE_LICENSE("GPL");
