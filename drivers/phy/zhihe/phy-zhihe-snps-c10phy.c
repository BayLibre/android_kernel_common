// SPDX-License-Identifier: GPL-2.0
/*
 * Zhihe A210 C10 USB 3.1 PHY driver
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

/* TCA (Type-C Assist) registers */
#define TCA_CLK_RST			0x00
#define TCA_CLK_RST_SW			BIT(9)

#define TCA_INTR_EN			0x04
#define TCA_INTR_EN_MASK		GENMASK(1, 0)

#define TCA_INTR_STS			0x08
#define TCA_INTR_STS_MASK		GENMASK(15, 0)

#define TCA_GCFG			0x10
#define TCA_GCFG_OP_MODE		GENMASK(1, 0)
#define TCA_GCFG_OP_MODE_SYSMODE	0
#define TCA_GCFG_OP_MODE_SYNCMODE	1

#define TCA_TCPC			0x14
#define TCA_TCPC_VALID			BIT(4)
#define TCA_TCPC_LOW_POWER_EN		BIT(3)
#define TCA_TCPC_ORIENTATION_NORMAL	BIT(2)
#define TCA_TCPC_MUX_CONTRL		GENMASK(1, 0)
#define TCA_TCPC_MUX_CONTRL_USB_CONN	1

#define TCA_SYSMODE_CFG			0x18
#define TCA_SYSMODE_TCPC_DISABLE	BIT(3)
#define TCA_SYSMODE_TCPC_FLIP		BIT(2)
#define TCA_SYSMODE_TCPC_CONN_MODE	GENMASK(1, 0)
#define TCA_SYSMODE_TCPC_CONN_DF	0x03
#define TCA_SYSMODE_TCPC_MASK		(TCA_SYSMODE_TCPC_DISABLE | \
					 TCA_SYSMODE_TCPC_FLIP | \
					 TCA_SYSMODE_TCPC_CONN_MODE)

#define TCA_GEN_STATUS			0x34

struct zhihe_c10phy_priv {
	void __iomem *tca_base;
	struct reset_control *c10phy_rst;
};

static void c10phy_tca_reset(struct zhihe_c10phy_priv *priv)
{
	u32 val;

	val = readl(priv->tca_base + TCA_CLK_RST);
	val &= ~TCA_CLK_RST_SW;
	writel(val, priv->tca_base + TCA_CLK_RST);

	udelay(100);

	val |= TCA_CLK_RST_SW;
	writel(val, priv->tca_base + TCA_CLK_RST);
}

/* Route the SuperSpeed lanes to USB, in normal orientation */
static void c10phy_tca_sys_cfg(struct zhihe_c10phy_priv *priv)
{
	void __iomem *base = priv->tca_base;
	u32 gen_status, val;

	writel(FIELD_PREP(TCA_GCFG_OP_MODE, TCA_GCFG_OP_MODE_SYSMODE),
	       base + TCA_GCFG);

	/* start from the crossbar configuration the TCA reports */
	gen_status = readl(base + TCA_GEN_STATUS);
	val = readl(base + TCA_SYSMODE_CFG) & ~TCA_SYSMODE_TCPC_MASK;
	val |= gen_status & TCA_SYSMODE_TCPC_MASK;
	writel(val, base + TCA_SYSMODE_CFG);

	val = readl(base + TCA_SYSMODE_CFG) | TCA_SYSMODE_TCPC_DISABLE;
	writel(val, base + TCA_SYSMODE_CFG);
	usleep_range(10, 20);

	val = readl(base + TCA_SYSMODE_CFG);
	val &= ~(TCA_SYSMODE_TCPC_FLIP | TCA_SYSMODE_TCPC_CONN_MODE);
	val |= TCA_SYSMODE_TCPC_CONN_DF;
	writel(val, base + TCA_SYSMODE_CFG);
	usleep_range(10, 20);

	val &= ~TCA_SYSMODE_TCPC_DISABLE;
	writel(val, base + TCA_SYSMODE_CFG);
}

static void c10phy_tca_sync_cfg(struct zhihe_c10phy_priv *priv)
{
	void __iomem *base = priv->tca_base;
	u32 val;

	writel(FIELD_PREP(TCA_GCFG_OP_MODE, TCA_GCFG_OP_MODE_SYNCMODE),
	       base + TCA_GCFG);

	/* enable the TCA ack and timeout interrupts */
	writel(TCA_INTR_STS_MASK, base + TCA_INTR_STS);
	writel(TCA_INTR_EN_MASK, base + TCA_INTR_EN);

	val = readl(base + TCA_TCPC);
	val &= ~(TCA_TCPC_MUX_CONTRL | TCA_TCPC_LOW_POWER_EN |
		 TCA_TCPC_ORIENTATION_NORMAL);
	val |= TCA_TCPC_MUX_CONTRL_USB_CONN | TCA_TCPC_VALID;
	writel(val, base + TCA_TCPC);
	usleep_range(10, 20);
}

static int c10phy_init(struct phy *phy)
{
	struct zhihe_c10phy_priv *priv = phy_get_drvdata(phy);
	int ret;

	ret = reset_control_deassert(priv->c10phy_rst);
	if (ret)
		return ret;

	c10phy_tca_reset(priv);
	c10phy_tca_sys_cfg(priv);
	usleep_range(10, 20);
	c10phy_tca_sync_cfg(priv);

	return 0;
}

static int c10phy_exit(struct phy *phy)
{
	struct zhihe_c10phy_priv *priv = phy_get_drvdata(phy);

	return reset_control_assert(priv->c10phy_rst);
}

static const struct phy_ops c10phy_ops = {
	.init = c10phy_init,
	.exit = c10phy_exit,
	.owner = THIS_MODULE,
};

static int c10phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct phy_provider *phy_provider;
	struct zhihe_c10phy_priv *priv;
	struct device_node *u3_np;
	struct phy *phy;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->tca_base = devm_platform_ioremap_resource_byname(pdev, "tca");
	if (IS_ERR(priv->tca_base))
		return dev_err_probe(dev, PTR_ERR(priv->tca_base),
				     "Couldn't get TCA register base\n");

	priv->c10phy_rst = devm_reset_control_get_shared(dev, "phy-rst");
	if (IS_ERR(priv->c10phy_rst))
		return dev_err_probe(dev, PTR_ERR(priv->c10phy_rst),
				     "Couldn't get phy-rst\n");

	u3_np = of_get_child_by_name(dev->of_node, "u3-port");
	if (!u3_np)
		return dev_err_probe(dev, -ENODEV, "no u3-port node\n");

	phy = devm_phy_create(dev, u3_np, &c10phy_ops);
	of_node_put(u3_np);
	if (IS_ERR(phy))
		return dev_err_probe(dev, PTR_ERR(phy), "failed to create usb phy\n");

	phy_set_drvdata(phy, priv);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider))
		return dev_err_probe(dev, PTR_ERR(phy_provider),
				     "failed to register phy provider\n");

	return 0;
}

static const struct of_device_id c10phy_of_match[] = {
	{ .compatible = "zhihe,a210-c10phy" },
	{ }
};
MODULE_DEVICE_TABLE(of, c10phy_of_match);

static struct platform_driver c10phy_driver = {
	.probe		= c10phy_probe,
	.driver		= {
		.name	= "phy-zhihe-a210-c10phy",
		.of_match_table	= c10phy_of_match,
	},
};
module_platform_driver(c10phy_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Zhihe A210 C10 USB 3.1 PHY driver");
MODULE_AUTHOR("Anonymous <Anonymous@zhcomputing.com>");
