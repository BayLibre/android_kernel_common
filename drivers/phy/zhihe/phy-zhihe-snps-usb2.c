// SPDX-License-Identifier: GPL-2.0
/*
 * Zhihe A210 USB 2.0 PHY driver
 *
 * Copyright (C) 2025, Anonymous <Anonymous@zhcomputing.com>
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>

/* USB20 BLK SYSREG registers */
#define PHY_CFG			0x4
#define PHY_DM_PULLDOWN		BIT(1)
#define PHY_DP_PULLDOWN		BIT(0)
#define PHY_DMDP_PULLDOWN	(PHY_DM_PULLDOWN | PHY_DP_PULLDOWN)

struct zhihe_usb2_phy_data {
	/* PHY2 is configured through the USB 3.1 controller's syscon */
	bool syscon;
	u32 pulldown;
	u32 init_reg;
	u32 init_val;
};

struct zhihe_usb2_phy_priv {
	const struct zhihe_usb2_phy_data *data;
	struct regmap *regs;
	struct reset_control *phy_rst;
	struct gpio_desc *pwren;
};

static const struct regmap_config zhihe_usb2_phy_regmap_cfg = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.disable_locking = true,
	.max_register = 0x40,
};

static int zhihe_usb2_phy_init(struct phy *phy)
{
	struct zhihe_usb2_phy_priv *priv = phy_get_drvdata(phy);
	int ret;

	gpiod_set_value_cansleep(priv->pwren, 1);

	if (priv->data->pulldown)
		regmap_set_bits(priv->regs, PHY_CFG, priv->data->pulldown);

	ret = reset_control_assert(priv->phy_rst);
	if (ret)
		goto err_pwren;

	usleep_range(100, 150);

	regmap_write(priv->regs, priv->data->init_reg, priv->data->init_val);

	ret = reset_control_deassert(priv->phy_rst);
	if (ret)
		goto err_pwren;

	usleep_range(80, 100);

	return 0;

err_pwren:
	gpiod_set_value_cansleep(priv->pwren, 0);
	return ret;
}

static int zhihe_usb2_phy_exit(struct phy *phy)
{
	struct zhihe_usb2_phy_priv *priv = phy_get_drvdata(phy);

	reset_control_assert(priv->phy_rst);
	gpiod_set_value_cansleep(priv->pwren, 0);

	return 0;
}

static const struct phy_ops zhihe_usb2_phy_ops = {
	.init = zhihe_usb2_phy_init,
	.exit = zhihe_usb2_phy_exit,
	.owner = THIS_MODULE,
};

static int zhihe_usb2_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct zhihe_usb2_phy_priv *priv;
	struct phy_provider *phy_provider;
	struct phy *phy;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->data = device_get_match_data(dev);

	if (priv->data->syscon) {
		priv->regs = syscon_regmap_lookup_by_phandle(dev->of_node, "syscon");
		if (IS_ERR(priv->regs))
			return dev_err_probe(dev, PTR_ERR(priv->regs),
					     "Failed to get regmap\n");
	} else {
		void __iomem *base = devm_platform_ioremap_resource(pdev, 0);

		if (IS_ERR(base))
			return PTR_ERR(base);

		priv->regs = devm_regmap_init_mmio(dev, base,
						   &zhihe_usb2_phy_regmap_cfg);
		if (IS_ERR(priv->regs))
			return dev_err_probe(dev, PTR_ERR(priv->regs),
					     "Couldn't create regmap\n");
	}

	priv->phy_rst = devm_reset_control_get_shared(dev, "phy-rst");
	if (IS_ERR(priv->phy_rst))
		return dev_err_probe(dev, PTR_ERR(priv->phy_rst),
				     "Couldn't get phy-rst\n");

	priv->pwren = devm_gpiod_get_optional(dev, "pwren", GPIOD_OUT_LOW);
	if (IS_ERR(priv->pwren))
		return dev_err_probe(dev, PTR_ERR(priv->pwren),
				     "Failed to get pwren GPIO\n");

	phy = devm_phy_create(dev, NULL, &zhihe_usb2_phy_ops);
	if (IS_ERR(phy))
		return dev_err_probe(dev, PTR_ERR(phy), "Couldn't create phy\n");

	phy_set_drvdata(phy, priv);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);
	if (IS_ERR(phy_provider))
		return dev_err_probe(dev, PTR_ERR(phy_provider),
				     "failed to register phy provider\n");

	return 0;
}

static const struct zhihe_usb2_phy_data a210_usb2_phy0_data = {
	.pulldown = PHY_DMDP_PULLDOWN,
	.init_reg = 0x000,
	.init_val = 0x40b67b5b,
};

static const struct zhihe_usb2_phy_data a210_usb2_phy1_data = {
	.pulldown = PHY_DMDP_PULLDOWN << 2,
	.init_reg = 0x000,
	.init_val = 0x40b67b5b,
};

static const struct zhihe_usb2_phy_data a210_usb2_phy2_data = {
	.syscon = true,
	.init_reg = 0x0a0,
	.init_val = 0x1000,
};

static const struct of_device_id zhihe_usb2_phy_of_match[] = {
	{ .compatible = "zhihe,a210-usb2-phy0", .data = &a210_usb2_phy0_data },
	{ .compatible = "zhihe,a210-usb2-phy1", .data = &a210_usb2_phy1_data },
	{ .compatible = "zhihe,a210-usb2-phy2", .data = &a210_usb2_phy2_data },
	{ }
};
MODULE_DEVICE_TABLE(of, zhihe_usb2_phy_of_match);

static struct platform_driver zhihe_usb2_phy_driver = {
	.probe		= zhihe_usb2_phy_probe,
	.driver = {
		.name	= "phy-a210-usb2",
		.of_match_table = zhihe_usb2_phy_of_match,
	},
};
module_platform_driver(zhihe_usb2_phy_driver);

MODULE_DESCRIPTION("Zhihe A210 USB 2.0 PHY driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Zhu Guangzhao <zhuzg.zhu@zhcomputing.com>");
