// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Icenowy Zheng <uwu@icenowy.me>
 */

#include <linux/dma-mapping.h>
#include <linux/irqreturn.h>
#include <linux/of.h>
#include <linux/of_graph.h>

#include "vs_crtc.h"
#include "vs_dc.h"
#include "vs_dc_top_regs.h"
#include "vs_drm.h"
#include "vs_hwdb.h"

static const struct regmap_config vs_dc_regmap_cfg = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = sizeof(u32),
	/* VSDC_OVL_CONFIG_EX(1) */
	.max_register = 0x2544,
};

static const struct of_device_id vs_dc_driver_dt_match[] = {
	{ .compatible = "verisilicon,dc" },
	/* Zhihe A210's DC8200 instance of this same IP block. */
	{ .compatible = "verisilicon,dc8200" },
	{},
};
MODULE_DEVICE_TABLE(of, vs_dc_driver_dt_match);

static irqreturn_t vs_dc_irq_handler(int irq, void *private)
{
	struct vs_dc *dc = private;
	u32 irqs;

	regmap_read(dc->regs, VSDC_TOP_IRQ_ACK, &irqs);

	vs_drm_handle_irq(dc, irqs);

	return IRQ_HANDLED;
}

static int vs_dc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vs_dc *dc;
	void __iomem *regs;
	unsigned int port_count, i;
	/* pix%u */
	char pixclk_name[14];
	int irq, ret;

	if (!dev->of_node) {
		dev_err(dev, "can't find DC devices\n");
		return -ENODEV;
	}

	port_count = of_graph_get_port_count(dev->of_node);
	if (!port_count) {
		dev_err(dev, "can't find DC downstream ports\n");
		return -ENODEV;
	}
	if (port_count > VSDC_MAX_OUTPUTS) {
		dev_err(dev, "too many DC downstream ports than possible\n");
		return -EINVAL;
	}

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(dev, "No suitable DMA available\n");
		return ret;
	}

	dc = devm_kzalloc(dev, sizeof(*dc), GFP_KERNEL);
	if (!dc)
		return -ENOMEM;

	dc->rsts[0].id = "core";
	dc->rsts[1].id = "axi";
	dc->rsts[2].id = "ahb";

	ret = devm_reset_control_bulk_get_optional_shared(dev, VSDC_RESET_COUNT,
							  dc->rsts);
	if (ret) {
		dev_err(dev, "can't get reset lines\n");
		return ret;
	}

	dc->core_clk = devm_clk_get_enabled(dev, "core");
	if (IS_ERR(dc->core_clk)) {
		dev_err(dev, "can't get core clock\n");
		return PTR_ERR(dc->core_clk);
	}

	dc->axi_clk = devm_clk_get_enabled(dev, "axi");
	if (IS_ERR(dc->axi_clk)) {
		dev_err(dev, "can't get axi clock\n");
		return PTR_ERR(dc->axi_clk);
	}

	dc->ahb_clk = devm_clk_get_enabled(dev, "ahb");
	if (IS_ERR(dc->ahb_clk)) {
		dev_err(dev, "can't get ahb clock\n");
		return PTR_ERR(dc->ahb_clk);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "can't get irq\n");
		return irq;
	}

	ret = reset_control_bulk_deassert(VSDC_RESET_COUNT, dc->rsts);
	if (ret) {
		dev_err(dev, "can't deassert reset lines\n");
		return ret;
	}

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs)) {
		dev_err(dev, "can't map registers");
		ret = PTR_ERR(regs);
		goto err_rst_assert;
	}

	dc->regs = devm_regmap_init_mmio(dev, regs, &vs_dc_regmap_cfg);
	if (IS_ERR(dc->regs)) {
		ret = PTR_ERR(dc->regs);
		goto err_rst_assert;
	}

	ret = vs_fill_chip_identity(dc->regs, &dc->identity);
	if (ret) {
		dev_err(dev, "unrecognized DC chip model=%#x rev=%#x customer=%#x\n",
			dc->identity.model, dc->identity.revision,
			dc->identity.customer_id);
		goto err_rst_assert;
	}

	dev_info(dev, "Found DC%x rev %x customer %x\n", dc->identity.model,
		 dc->identity.revision, dc->identity.customer_id);

	/*
	 * DC_CLK_GATTING (0x1a28/0x1a2c, per-block internal clock-gating
	 * enable bits) and DC_QOS_CONFIG (0x1a38, AXI read/write QoS
	 * priority) are both unconditionally written by zhihesdk's own
	 * vs_dc_hw.c at probe time, and neither is ever touched by this
	 * (upstream) driver - confirmed via live register diff against a
	 * working zhihesdk buildroot boot that both sit at different values
	 * here (0x00000e07/0x00000e07/0x00000010) than there
	 * (0x00000000/0x00000000/0x000000c0). Clearing all internal clock
	 * gates and matching the vendor's QoS priority removes two more
	 * candidates (starved-of-clock internal sub-blocks; starved-of-
	 * memory-bandwidth fetch engine) for why the framebuffer's own
	 * pixel data - otherwise confirmed correctly fetched, formatted,
	 * positioned and blended - might still never reach the output.
	 */
	regmap_write(dc->regs, 0x1a28, 0x0);
	regmap_write(dc->regs, 0x1a2c, 0x0);
	regmap_write(dc->regs, 0x1a38, 0xc0);

	/*
	 * DC_FRAMEBUFFER_WATER_MARK (0x1ce8/+0x4) and DC_OVERLAY_WATER_MARK
	 * (0x1dc0/+0x4/+0x8/+0xc) are FIFO fetch-watermark thresholds for the
	 * framebuffer and overlay layer types respectively, unconditionally
	 * written to 0x3000 by zhihesdk's own vs_dc_hw.c (dc_hw_init(), the
	 * same init function DC_CLK_GATTING/DC_QOS_CONFIG above come from) -
	 * confirmed via live register diff against a working zhihesdk
	 * buildroot boot that both sit at 0 here (this driver never writes
	 * them, even for the framebuffer layer type it does use) vs 0x3000
	 * there.
	 */
	regmap_write(dc->regs, 0x1ce8, 0x3000);
	regmap_write(dc->regs, 0x1cec, 0x3000);
	regmap_write(dc->regs, 0x1dc0, 0x3000);
	regmap_write(dc->regs, 0x1dc4, 0x3000);
	regmap_write(dc->regs, 0x1dc8, 0x3000);
	regmap_write(dc->regs, 0x1dcc, 0x3000);

	if (port_count > dc->identity.display_count) {
		dev_err(dev, "too many downstream ports than HW capability\n");
		ret = -EINVAL;
		goto err_rst_assert;
	}

	for (i = 0; i < dc->identity.display_count; i++) {
		snprintf(pixclk_name, sizeof(pixclk_name), "pix%u", i);
		dc->pix_clk[i] = devm_clk_get(dev, pixclk_name);
		if (IS_ERR(dc->pix_clk[i])) {
			dev_err(dev, "can't get pixel clk %u\n", i);
			ret = PTR_ERR(dc->pix_clk[i]);
			goto err_rst_assert;
		}
	}

	ret = devm_request_irq(dev, irq, vs_dc_irq_handler, 0,
			       dev_name(dev), dc);
	if (ret) {
		dev_err(dev, "can't request irq\n");
		goto err_rst_assert;
	}

	dev_set_drvdata(dev, dc);

	ret = vs_drm_initialize(dc, pdev);
	if (ret)
		goto err_rst_assert;

	return 0;

err_rst_assert:
	reset_control_bulk_assert(VSDC_RESET_COUNT, dc->rsts);
	return ret;
}

static void vs_dc_remove(struct platform_device *pdev)
{
	struct vs_dc *dc = dev_get_drvdata(&pdev->dev);

	vs_drm_finalize(dc);

	dev_set_drvdata(&pdev->dev, NULL);

	reset_control_bulk_assert(VSDC_RESET_COUNT, dc->rsts);
}

static void vs_dc_shutdown(struct platform_device *pdev)
{
	struct vs_dc *dc = dev_get_drvdata(&pdev->dev);

	vs_drm_shutdown_handler(dc);
}

static struct platform_driver vs_dc_platform_driver = {
	.probe = vs_dc_probe,
	.remove = vs_dc_remove,
	.shutdown = vs_dc_shutdown,
	.driver = {
		.name = "verisilicon-dc",
		.of_match_table = vs_dc_driver_dt_match,
	},
};

module_platform_driver(vs_dc_platform_driver);

MODULE_AUTHOR("Icenowy Zheng <uwu@icenowy.me>");
MODULE_DESCRIPTION("Verisilicon display controller driver");
MODULE_LICENSE("GPL");
