// SPDX-License-Identifier: GPL-2.0
/*
 * USB Typec-C USB4 Mode driver
 *
 * Copyright 2025 Google LLC
 */

#include <linux/module.h>
#include <linux/usb/typec_altmode.h>

static int usb4_altmode_activate(struct typec_altmode *alt, int activate)
{
	if (activate)
		return typec_altmode_enter(alt, NULL);
	return typec_altmode_exit(alt);
}

static const struct typec_altmode_ops usb4_altmode_ops = {
	.activate	= usb4_altmode_activate,
};

static int usb4_altmode_probe(struct typec_altmode *alt)
{
	alt->desc = "USB4";
	typec_altmode_set_ops(alt, &usb4_altmode_ops);

	return 0;
}

static const struct typec_device_id usb4_typec_id[] = {
	{ USB_TYPEC_USB4_SID },
	{ }
};
MODULE_DEVICE_TABLE(typec, usb4_typec_id);

static struct typec_altmode_driver usb4_altmode_driver = {
	.id_table = usb4_typec_id,
	.probe = usb4_altmode_probe,
	.driver = {
		.name = "typec-usb4",
	}
};
module_typec_altmode_driver(usb4_altmode_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("USB4 Type-C Mode");
