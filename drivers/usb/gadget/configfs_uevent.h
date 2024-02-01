/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2024 Google LLC
 */
#ifndef __CONFIGFS_UEVENT_H
#define __CONFIGFS_UEVENT_H

#ifdef CONFIG_USB_CONFIGFS_UEVENT
#include <linux/usb/configfs_uevent.h>

int android_class_create(void);
void android_class_destroy(void);
int android_device_create(struct android_uevent_opts *opts);
void android_device_destroy(struct android_uevent_opts *opts);
void android_set_connected(struct android_uevent_opts *opts, bool connected);
void android_set_configured(struct android_uevent_opts *opts, bool configured);

#else
struct android_uevent_opts {};

static inline int android_class_create(void)
{
	return 0;
}

static inline void android_class_destroy(void)
{
}

static inline int android_device_create(struct android_uevent_opts *opts)
{
	return 0;
}

static inline void android_device_destroy(struct android_uevent_opts *opts)
{
}

static inline void android_set_connected(struct android_uevent_opts *opts,
	       bool connected)
{
}

static inline void android_set_configured(struct android_uevent_opts *opts,
	       bool configured)
{
}
#endif /* CONFIG_USB_CONFIGFS_UEVENT */
#endif /* __CONFIGFS_UEVENT_H */
