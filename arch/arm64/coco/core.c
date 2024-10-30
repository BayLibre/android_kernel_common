// SPDX-License-Identifier: GPL-2.0-only

#include <linux/export.h>
#include <linux/cc_platform.h>
#include <linux/virtio_anchor.h>

bool noinstr cc_platform_has(enum cc_attr attr)
{
	return false;
}
EXPORT_SYMBOL_GPL(cc_platform_has);

static int __init aarch64_coco_init(void)
{
	/* Set restricted memory access for virtio. */
	virtio_set_mem_acc_cb(virtio_require_restricted_mem_acc);
}
arch_initcall(coco_init);
