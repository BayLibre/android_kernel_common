// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Zhihe Computing Limited.
 */

#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "clk-helper.h"

#define div_mask(d)	((1 << (d->width)) - 1)

static DEFINE_SPINLOCK(zhihe_clk_lock);

struct clk_zhihediv {
	struct clk_divider divider;
	enum zhihe_div_type div_type;
	u16 min_div;
	u16 max_div;
	u8 sync_en;
};

struct clk_zhihegate {
	struct clk_gate gate;
	unsigned int *share_count;
};

static inline struct clk_zhihediv *to_clk_zhihediv(struct clk_hw *hw)
{
	struct clk_divider *divider = to_clk_divider(hw);

	return container_of(divider, struct clk_zhihediv, divider);
}

static unsigned long clk_zhihediv_recalc_rate(struct clk_hw *hw,
					      unsigned long parent_rate)
{
	return clk_divider_ops.recalc_rate(hw, parent_rate);
}

static int clk_zhihediv_determine_rate(struct clk_hw *hw,
				       struct clk_rate_request *req)
{
	return clk_divider_ops.determine_rate(hw, req);
}

static int clk_zhihediv_set_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long parent_rate)
{
	struct clk_zhihediv *zhihe_div = to_clk_zhihediv(hw);
	struct clk_divider *div = to_clk_divider(hw);
	unsigned int divider, value;
	unsigned long flags;
	u32 val;

	/*
	 * clk_divider_ops rounds the divider up; pick the closest one so that
	 * the rate recalc_rate() reports back matches the requested one.
	 */
	divider = DIV64_U64_ROUND_CLOSEST(parent_rate, rate);

	/* DIV is zero based divider, but CDE is not */
	if (zhihe_div->div_type == MUX_TYPE_DIV)
		value = divider;
	else
		value = divider - 1;

	/* handle the div valid range */
	if (value > zhihe_div->max_div)
		value = zhihe_div->max_div;
	if (value < zhihe_div->min_div)
		value = zhihe_div->min_div;

	spin_lock_irqsave(div->lock, flags);

	val = readl(div->reg);

	if (zhihe_div->sync_en != NO_DIV_EN) {
		val &= ~BIT(zhihe_div->sync_en);
		writel(val, div->reg);
		udelay(1);
	}

	val &= ~(div_mask(div) << div->shift);
	val |= value << div->shift;
	writel(val, div->reg);

	if (zhihe_div->sync_en != NO_DIV_EN) {
		udelay(1);
		val |= BIT(zhihe_div->sync_en);
		writel(val, div->reg);
	}

	spin_unlock_irqrestore(div->lock, flags);

	return 0;
}

static const struct clk_ops clk_zhihediv_ops = {
	.recalc_rate = clk_zhihediv_recalc_rate,
	.determine_rate = clk_zhihediv_determine_rate,
	.set_rate = clk_zhihediv_set_rate,
};

static struct clk_hw *zhihe_clk_divider(struct device *dev, const char *name,
					const char *parent, void __iomem *reg,
					const struct zhihe_clk_info *info,
					bool closest)
{
	struct clk_zhihediv *zhihe_div;
	struct clk_init_data init = {};
	u16 max = (1 << info->width) - 1;
	int ret;

	zhihe_div = devm_kzalloc(dev, sizeof(*zhihe_div), GFP_KERNEL);
	if (!zhihe_div)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &clk_zhihediv_ops;
	init.flags = CLK_SET_RATE_PARENT;
	init.parent_names = &parent;
	init.num_parents = 1;

	zhihe_div->divider.reg = reg;
	zhihe_div->divider.shift = info->bit_idx;
	zhihe_div->divider.width = info->width;
	zhihe_div->divider.lock = &zhihe_clk_lock;
	zhihe_div->divider.hw.init = &init;
	zhihe_div->sync_en = info->divider.sync;
	zhihe_div->div_type = info->divider.div_type;
	if (zhihe_div->div_type == MUX_TYPE_DIV)
		zhihe_div->divider.flags = CLK_DIVIDER_ONE_BASED;
	if (closest)
		zhihe_div->divider.flags |= CLK_DIVIDER_ROUND_CLOSEST;
	zhihe_div->min_div = min(info->divider.min, max);
	zhihe_div->max_div = min(info->divider.max, max);

	ret = devm_clk_hw_register(dev, &zhihe_div->divider.hw);
	if (ret)
		return ERR_PTR(ret);

	return &zhihe_div->divider.hw;
}

static inline struct clk_zhihegate *to_clk_zhihegate(struct clk_hw *hw)
{
	struct clk_gate *gate = to_clk_gate(hw);

	return container_of(gate, struct clk_zhihegate, gate);
}

static int clk_zhihe_gate_share_is_enabled(struct clk_hw *hw)
{
	return clk_gate_ops.is_enabled(hw);
}

static int clk_zhihe_gate_share_enable(struct clk_hw *hw)
{
	struct clk_zhihegate *zhihe_gate = to_clk_zhihegate(hw);

	if ((*zhihe_gate->share_count)++ > 0)
		return 0;

	return clk_gate_ops.enable(hw);
}

static void clk_zhihe_gate_share_disable(struct clk_hw *hw)
{
	struct clk_zhihegate *zhihe_gate = to_clk_zhihegate(hw);

	if (WARN_ON(*zhihe_gate->share_count == 0))
		return;
	if (--(*zhihe_gate->share_count) > 0)
		return;

	clk_gate_ops.disable(hw);
}

static void clk_zhihe_gate_share_disable_unused(struct clk_hw *hw)
{
	struct clk_zhihegate *zhihe_gate = to_clk_zhihegate(hw);

	if (*zhihe_gate->share_count == 0)
		clk_gate_ops.disable(hw);
}

static const struct clk_ops clk_zhihegate_share_ops = {
	.enable = clk_zhihe_gate_share_enable,
	.disable = clk_zhihe_gate_share_disable,
	.disable_unused = clk_zhihe_gate_share_disable_unused,
	.is_enabled = clk_zhihe_gate_share_is_enabled,
};

static struct clk_hw *zhihe_clk_gate_shared(struct device *dev,
					    const char *name,
					    const char *parent,
					    void __iomem *reg, u8 bit_idx,
					    unsigned int *share_count)
{
	struct clk_zhihegate *zhihe_gate;
	struct clk_init_data init = {};
	int ret;

	zhihe_gate = devm_kzalloc(dev, sizeof(*zhihe_gate), GFP_KERNEL);
	if (!zhihe_gate)
		return ERR_PTR(-ENOMEM);

	zhihe_gate->gate.reg = reg;
	zhihe_gate->gate.bit_idx = bit_idx;
	zhihe_gate->gate.lock = &zhihe_clk_lock;
	zhihe_gate->gate.hw.init = &init;
	zhihe_gate->share_count = share_count;

	init.name = name;
	init.ops = &clk_zhihegate_share_ops;
	init.flags = CLK_SET_RATE_PARENT;
	init.parent_names = &parent;
	init.num_parents = 1;

	ret = devm_clk_hw_register(dev, &zhihe_gate->gate.hw);
	if (ret)
		return ERR_PTR(ret);

	return &zhihe_gate->gate.hw;
}

int zhihe_register_clocks(struct platform_device *pdev,
			  const struct zhihe_clk_subsys *subsys,
			  struct clk_hw_onecell_data *clk_data)
{
	struct device *dev = &pdev->dev;
	void __iomem **bases;
	int i;

	bases = devm_kcalloc(dev, subsys->num_regs, sizeof(*bases), GFP_KERNEL);
	if (!bases)
		return -ENOMEM;

	for (i = 0; i < subsys->num_regs; i++) {
		bases[i] = devm_platform_ioremap_resource_byname(pdev,
								 subsys->reg_names[i]);
		if (IS_ERR(bases[i]))
			return PTR_ERR(bases[i]);
	}

	for (i = 0; i < subsys->num_info; i++) {
		const struct zhihe_clk_info *info = &subsys->info[i];
		void __iomem *reg = bases[info->reg] + info->shift;
		const char *parent = info->parent;
		const char *name = info->name;
		struct clk_hw *hw;

		switch (info->type) {
		case CLK_TYPE_FIXED:
			hw = devm_clk_hw_register_fixed_rate(dev, name, parent, 0,
							     info->fixed.freq);
			break;
		case CLK_TYPE_FIXED_FACTOR:
			hw = devm_clk_hw_register_fixed_factor(dev, name, parent,
							       CLK_SET_RATE_PARENT,
							       info->fixed_factor.mult,
							       info->fixed_factor.div);
			break;
		case CLK_TYPE_PLL:
			hw = zhihe_pll(dev, name, parent, reg, info->pll);
			break;
		case CLK_TYPE_DIVIDER:
			hw = zhihe_clk_divider(dev, name, parent, reg, info, false);
			break;
		case CLK_TYPE_DIVIDER_CLOSEST:
			hw = zhihe_clk_divider(dev, name, parent, reg, info, true);
			break;
		case CLK_TYPE_GATE:
			hw = devm_clk_hw_register_gate(dev, name, parent,
						       CLK_SET_RATE_PARENT, reg,
						       info->bit_idx, 0,
						       &zhihe_clk_lock);
			break;
		case CLK_TYPE_GATE_SHARED:
			hw = zhihe_clk_gate_shared(dev, name, parent, reg,
						   info->bit_idx,
						   info->gate_shared.share_count);
			break;
		case CLK_TYPE_MUX:
			hw = devm_clk_hw_register_mux(dev, name, info->mux.parents,
						      info->mux.num_parents,
						      info->mux.flags, reg,
						      info->bit_idx, info->width,
						      0, &zhihe_clk_lock);
			break;
		default:
			hw = ERR_PTR(-EINVAL);
			break;
		}

		if (IS_ERR(hw))
			return dev_err_probe(dev, PTR_ERR(hw),
					     "failed to register clock %s\n", name);

		clk_data->hws[info->id] = hw;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(zhihe_register_clocks);

MODULE_AUTHOR("dong.yan <yand@zhihecomputing.com>");
MODULE_DESCRIPTION("Zhihe SoC clock core (divider/gate/PLL helpers)");
MODULE_LICENSE("GPL");
