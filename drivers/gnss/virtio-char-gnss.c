// SPDX-License-Identifier: GPL-2.0
/*
 * virtio serial driver for GNSS. This driver requires the serdev binding
 *
 * Copyright 2021 Google LLC
 */

#include <linux/device.h>
#include <linux/gnss.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/serdev.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/virtio.h>
#include <linux/virtio_console.h>

#include "serial.h"

struct virtio_char_gnss_dev {
    struct virtio_device *vdev;
	struct cdev *cdev;
	struct device *dev;
	struct virtqueue *in_vq, *out_vq;
    int chr_major;
    u32 id;
};

static const unsigned int features[] = {
};

static const struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_CHAR_GNSS, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static int virtio_gnss_probe(struct virtio_device *vdev)
{
    return 0;
}

static void virtio_gnss_remove(struct virtio_device *vdev)
{
}

#ifdef CONFIG_PM_SLEEP
static int virtio_gnss_freeze(struct virtio_device *vdev)
{
    return 0;
}
static int virtio_gnss_restore(struct virtio_device *vdev)
{
    return 0;
}
#endif

static struct virtio_driver virtio_char_gnss_driver = {
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name =	KBUILD_MODNAME,
	.driver.owner =	THIS_MODULE,
	.id_table =	id_table,
	.probe =	virtio_gnss_probe,
	.remove =	virtio_gnss_remove,
#ifdef CONFIG_PM_SLEEP
	.freeze =	virtio_gnss_freeze,
	.restore =	virtio_gnss_restore,
#endif
};

static int __init gnss_virtio_init(void)
{

    register_virtio_driver(&virtio_char_gnss_driver);
	return 0;
}

static void __exit gnss_virtio_exit(void)
{

}

module_init(gnss_virtio_init);
module_exit(gnss_virtio_exit);

MODULE_AUTHOR("Bo Hu <bohu@google.com>");
MODULE_DESCRIPTION("GNSS virtio serial driver");
MODULE_SOFTDEP("pre: gnss_serial");
MODULE_LICENSE("GPL v2");
