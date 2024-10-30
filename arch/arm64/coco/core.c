// SPDX-License-Identifier: GPL-2.0-only

#include <linux/export.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/cc_platform.h>
#include <linux/virtio_anchor.h>

static u64 arm64_cc_status;

#define BITS_PER_U64 ((u32)(sizeof(u64) * 8))

bool noinstr cc_platform_has(enum cc_attr attr)
{
	u64 mask;

	if (attr >= BITS_PER_U64) {
		pr_err("%s: Invalid attr %d\n", __func__, attr);
		return false;
	}

	mask = 1 << attr;

	return (arm64_cc_status & mask) != 0;
}
EXPORT_SYMBOL_GPL(cc_platform_has);

void cc_platform_set(enum cc_attr attr)
{
	u64 mask;

	if (attr >= BITS_PER_U64) {
		pr_err("%s: Invalid attr %d\n", __func__, attr);
		return;
	}

	mask = 1 << attr;

	switch (attr) {
#ifdef CONFIG_HARDENED_GUEST
	case CC_ATTR_GUEST_HARDENED:
		arm64_cc_status |= mask;
		break;
#endif
	default:
		break;
	}
}
EXPORT_SYMBOL_GPL(cc_platform_set);

void cc_platform_clear(enum cc_attr attr)
{
	u64 mask;

	if (attr >= BITS_PER_U64) {
		pr_err("%s: Invalid attr %d\n", __func__, attr);
		return;
	}

	mask = 1 << attr;

	switch (attr) {
#ifdef CONFIG_HARDENED_GUEST
	case CC_ATTR_GUEST_HARDENED:
		arm64_cc_status &= ~mask;
		break;
#endif
	default:
		break;
	}
}
EXPORT_SYMBOL_GPL(cc_platform_clear);

static int __init aarch64_coco_init(void)
{
#ifdef CONFIG_HARDENED_GUEST
	cc_platform_set(CC_ATTR_GUEST_HARDENED);

	/* Set restricted memory access for virtio. */
	virtio_set_mem_acc_cb(virtio_require_restricted_mem_acc);
#endif
	return 0;
}
arch_initcall(aarch64_coco_init);
