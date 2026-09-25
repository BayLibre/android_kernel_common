// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Zhihe Computing Limited.
 */

#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/math.h>
#include <linux/slab.h>

#include "clk-helper.h"

#define ZHIHE_PLL_CFG1		0x04
#define ZHIHE_POSTDIV2_SHIFT	24
#define ZHIHE_POSTDIV2_MASK	GENMASK(26, 24)
#define ZHIHE_POSTDIV1_SHIFT	20
#define ZHIHE_POSTDIV1_MASK	GENMASK(22, 20)
#define ZHIHE_FBDIV_SHIFT	8
#define ZHIHE_FBDIV_MASK	GENMASK(19, 8)
#define ZHIHE_REFDIV_SHIFT	0
#define ZHIHE_REFDIV_MASK	GENMASK(5, 0)
#define ZHIHE_DSMPD_MASK	BIT(24)
#define ZHIHE_DACPD_MASK	BIT(25)
#define ZHIHE_FRAC_MASK		GENMASK(23, 0)
#define ZHIHE_FRAC_SHIFT	0
#define ZHIHE_FRAC_DIV		BIT(24)

#define LOCK_TIMEOUT_US		10000

struct clk_zhihepll {
	struct clk_hw hw;
	void __iomem *base;
	const struct zhihe_clk_info_pll *info;
};

#define to_clk_zhihepll(_hw) container_of(_hw, struct clk_zhihepll, hw)

static void __iomem *clk_zhihe_pll_cfg0(struct clk_zhihepll *pll)
{
	return pll->base + pll->info->cfg0_reg_off;
}

static void __iomem *clk_zhihe_pll_cfg1(struct clk_zhihepll *pll)
{
	return clk_zhihe_pll_cfg0(pll) + ZHIHE_PLL_CFG1;
}

static int clk_zhihe_pll_wait_lock(struct clk_zhihepll *pll)
{
	u32 val;

	return readl_poll_timeout(pll->base + pll->info->pll_sts_off, val,
				  val & pll->info->pll_lock_bit, 0,
				  LOCK_TIMEOUT_US);
}

static int clk_zhihe_pll_prepare(struct clk_hw *hw)
{
	struct clk_zhihepll *pll = to_clk_zhihepll(hw);
	u32 val;

	val = readl_relaxed(clk_zhihe_pll_cfg1(pll));
	if (!(val & pll->info->pll_rst_bit))
		return 0;

	/* Hold the PLL in reset for 3us before releasing it */
	udelay(3);
	val &= ~pll->info->pll_rst_bit;
	writel_relaxed(val, clk_zhihe_pll_cfg1(pll));

	return clk_zhihe_pll_wait_lock(pll);
}

static int clk_zhihe_pll_is_prepared(struct clk_hw *hw)
{
	struct clk_zhihepll *pll = to_clk_zhihepll(hw);
	u32 val;

	val = readl_relaxed(clk_zhihe_pll_cfg1(pll));

	return (val & pll->info->pll_rst_bit) ? 0 : 1;
}

static void clk_zhihe_pll_unprepare(struct clk_hw *hw)
{
	struct clk_zhihepll *pll = to_clk_zhihepll(hw);
	u32 val;

	val = readl_relaxed(clk_zhihe_pll_cfg1(pll));
	val |= pll->info->pll_rst_bit;
	writel_relaxed(val, clk_zhihe_pll_cfg1(pll));
}

static unsigned long clk_zhihe_pll_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	struct clk_zhihepll *pll = to_clk_zhihepll(hw);
	u32 refdiv, fbdiv, frac;
	u32 pll_cfg0, pll_cfg1;
	u64 fvco = 0;

	pll_cfg0 = readl_relaxed(clk_zhihe_pll_cfg0(pll));
	pll_cfg1 = readl_relaxed(clk_zhihe_pll_cfg1(pll));
	refdiv = (pll_cfg0 & ZHIHE_REFDIV_MASK) >> ZHIHE_REFDIV_SHIFT;
	fbdiv = (pll_cfg0 & ZHIHE_FBDIV_MASK) >> ZHIHE_FBDIV_SHIFT;
	frac = (pll_cfg1 & ZHIHE_FRAC_MASK) >> ZHIHE_FRAC_SHIFT;

	/*
	 * INT mode:  FOUTVCO = FREF * FBDIV / REFDIV
	 * FRAC mode: FOUTVCO = (FREF * FBDIV + FREF * FRAC / BIT(24)) / REFDIV
	 */
	if (pll->info->pll_mode == PLL_MODE_FRAC)
		fvco = (parent_rate * frac) / ZHIHE_FRAC_DIV;

	fvco += (parent_rate * fbdiv);
	do_div(fvco, refdiv);

	return fvco;
}

static const struct zhihe_pll_rate_table *
zhihe_get_pll_vco_settings(struct clk_zhihepll *pll, unsigned long rate)
{
	const struct zhihe_pll_rate_table *rate_table = pll->info->rate_table;
	int i;

	for (i = 0; i < pll->info->rate_count; i++)
		if (rate == rate_table[i].vco_rate)
			return &rate_table[i];

	return NULL;
}

static inline bool clk_zhihe_pll_change(struct clk_zhihepll *pll,
					const struct zhihe_pll_rate_table *rate)
{
	u32 refdiv_old, fbdiv_old, postdiv1_old, postdiv2_old, frac_old;
	u32 cfg0, cfg1;
	bool pll_changed;

	cfg0 = readl_relaxed(clk_zhihe_pll_cfg0(pll));
	cfg1 = readl_relaxed(clk_zhihe_pll_cfg1(pll));

	refdiv_old = (cfg0 & ZHIHE_REFDIV_MASK) >> ZHIHE_REFDIV_SHIFT;
	fbdiv_old = (cfg0 & ZHIHE_FBDIV_MASK) >> ZHIHE_FBDIV_SHIFT;
	postdiv1_old = (cfg0 & ZHIHE_POSTDIV1_MASK) >> ZHIHE_POSTDIV1_SHIFT;
	postdiv2_old = (cfg0 & ZHIHE_POSTDIV2_MASK) >> ZHIHE_POSTDIV2_SHIFT;
	frac_old = (cfg1 & ZHIHE_FRAC_MASK) >> ZHIHE_FRAC_SHIFT;

	pll_changed = rate->refdiv != refdiv_old || rate->fbdiv != fbdiv_old ||
		      rate->postdiv1 != postdiv1_old || rate->postdiv2 != postdiv2_old;
	if (pll->info->pll_mode == PLL_MODE_FRAC)
		pll_changed |= (rate->frac != frac_old);

	return pll_changed;
}

static int clk_zhihe_pll_set_rate(struct clk_hw *hw, unsigned long drate,
				  unsigned long prate)
{
	struct clk_zhihepll *pll = to_clk_zhihepll(hw);
	const struct zhihe_pll_rate_table *rate;
	u32 tmp, div_val;
	int ret;

	rate = zhihe_get_pll_vco_settings(pll, drate);
	if (!rate) {
		pr_err("%s: Invalid rate : %lu for pll clk %s\n", __func__,
		       drate, clk_hw_get_name(hw));
		return -EINVAL;
	}

	if (!clk_zhihe_pll_change(pll, rate))
		return 0;

	/* Enable RST */
	tmp = readl_relaxed(clk_zhihe_pll_cfg1(pll));
	tmp |= pll->info->pll_rst_bit;
	writel_relaxed(tmp, clk_zhihe_pll_cfg1(pll));

	div_val = (rate->refdiv << ZHIHE_REFDIV_SHIFT) |
		  (rate->fbdiv << ZHIHE_FBDIV_SHIFT) |
		  (rate->postdiv1 << ZHIHE_POSTDIV1_SHIFT) |
		  (rate->postdiv2 << ZHIHE_POSTDIV2_SHIFT);
	writel_relaxed(div_val, clk_zhihe_pll_cfg0(pll));

	if (pll->info->pll_mode == PLL_MODE_FRAC) {
		tmp &= ~(ZHIHE_FRAC_MASK << ZHIHE_FRAC_SHIFT);
		tmp |= rate->frac;
		writel_relaxed(tmp, clk_zhihe_pll_cfg1(pll));
	}

	udelay(3);

	/* Disable RST */
	tmp &= ~pll->info->pll_rst_bit;
	writel_relaxed(tmp, clk_zhihe_pll_cfg1(pll));

	/* Wait Lock, ~20us cost */
	ret = clk_zhihe_pll_wait_lock(pll);
	if (ret)
		return ret;

	/* HW requires 30us for pll stable */
	udelay(30);

	return 0;
}

static int clk_zhihe_pllvco_determine_rate(struct clk_hw *hw,
					   struct clk_rate_request *req)
{
	struct clk_zhihepll *pll = to_clk_zhihepll(hw);
	const struct zhihe_pll_rate_table *rate_table = pll->info->rate_table;
	unsigned long best = 0, now;
	unsigned int i, best_i = 0;

	for (i = 0; i < pll->info->rate_count; i++) {
		now = rate_table[i].vco_rate;

		if (req->rate == now)
			return 0;

		if (abs_diff(now, req->rate) < abs_diff(best, req->rate)) {
			best = now;
			best_i = i;
		}
	}

	/* return the closest supported rate */
	req->rate = rate_table[best_i].vco_rate;
	return 0;
}

static const struct clk_ops clk_zhihe_pllvco_ops = {
	.prepare	= clk_zhihe_pll_prepare,
	.unprepare	= clk_zhihe_pll_unprepare,
	.is_prepared	= clk_zhihe_pll_is_prepared,
	.recalc_rate	= clk_zhihe_pll_recalc_rate,
	.determine_rate	= clk_zhihe_pllvco_determine_rate,
	.set_rate	= clk_zhihe_pll_set_rate,
};

struct clk_hw *zhihe_pll(struct device *dev, const char *name,
			 const char *parent_name, void __iomem *base,
			 const struct zhihe_clk_info_pll *pll_clk)
{
	struct clk_init_data init = {};
	struct clk_zhihepll *pll;
	u32 val;
	int ret;

	pll = devm_kzalloc(dev, sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &clk_zhihe_pllvco_ops;
	init.parent_names = &parent_name;
	init.num_parents = 1;

	pll->base = base;
	pll->hw.init = &init;
	pll->info = pll_clk;

	val = readl_relaxed(clk_zhihe_pll_cfg1(pll));
	val &= ~pll_clk->pll_bypass_bit;
	if (pll_clk->pll_mode == PLL_MODE_FRAC)
		val &= ~(ZHIHE_DSMPD_MASK | ZHIHE_DACPD_MASK);
	else
		val |= ZHIHE_DSMPD_MASK | ZHIHE_DACPD_MASK;
	writel_relaxed(val, clk_zhihe_pll_cfg1(pll));

	ret = devm_clk_hw_register(dev, &pll->hw);
	if (ret)
		return ERR_PTR(ret);

	return &pll->hw;
}
