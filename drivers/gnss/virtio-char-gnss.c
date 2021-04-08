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

#include "serial.h"


static int __init gnss_virtio_init(void)
{

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
