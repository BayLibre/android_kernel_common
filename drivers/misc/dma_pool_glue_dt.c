// SPDX-License-Identifier: GPL-2.0

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/dma-mapping.h>
#include <linux/dma-map-ops.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/of_address.h>

static int __init uvc_urb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct reserved_mem *rmem;
	int ret;

	dev_info(dev, "calling uvc_urb_probe()\n");

	rmem = of_reserved_mem_lookup(dev->of_node);
	if (!rmem) {
		dev_err(dev, "uvc_urb: failed to lookup reserved memory\n");
		return -EINVAL;
	}

	if (!rmem->size || (rmem->size > ULONG_MAX)) {
		dev_err(dev, "uvc_urb: invalid memory region size\n");
		return -EINVAL;
	}

	if (!PAGE_ALIGNED(rmem->base) || !PAGE_ALIGNED(rmem->size)) {
		dev_err(dev, "uvc_urb: memory region must be page-aligned\n");
		return -EINVAL;
	}

	ret = dma_declare_coherent_memory(dev, rmem->base, rmem->base, rmem->size);
	if (ret) {
		dev_err(dev, "uvc_urb: Failed to declare coherent memory pool: %d\n", ret);
		return ret;
	}

	dev_info(dev, "uvc_urb: Successfully attached custom DMA pool from DT\n");

	return 0;
}

static int uvc_urb_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	dma_release_coherent_memory(dev);
	dev_info(dev, "uvc_urb: Released custom DMA pool\n");

	return 0;
}

// maybe we could parameterize the compatible string?
static const struct of_device_id uvc_urb_of_match[] = {
	{ .compatible = "google,uvc-urb" },
	{},
};

static struct platform_driver uvc_urb_driver = {
	.remove = uvc_urb_remove,
	.driver = {
		.name = "uvc-urb-driver",
		.of_match_table = uvc_urb_of_match,
	},
};

static int __init uvc_urb_init(void)
{
	pr_info("calling uvc_urb_init");
	int ret = platform_driver_probe(&uvc_urb_driver, uvc_urb_probe);
	pr_info("ret = %d\n", ret);

	return (ret == -ENODEV) ? 0 : ret;
}

static void __exit uvc_urb_exit(void)
{
	platform_driver_unregister(&uvc_urb_driver);
}

module_init(uvc_urb_init);
module_exit(uvc_urb_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DT-aware glue driver for allocating from a custom DMA pool.");
