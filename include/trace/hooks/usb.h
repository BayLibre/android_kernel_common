/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM usb
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_USB_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_USB_H
#include <linux/tracepoint.h>
#include <linux/usb/composite.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */

/*
DECLARE_HOOK(android_vh_usb_configfs_create_func_dev,
	TP_PROTO(char *name, struct device **dev),
	TP_ARGS(name, dev));
*/
DECLARE_HOOK(android_vh_usb_configfs_pre_setup,
	     TP_PROTO(struct list_head *available_func, const struct usb_ctrlrequest *c,
		      int *value),
	     TP_ARGS(available_func, c, value));

DECLARE_HOOK(android_vh_usb_configfs_post_setup,
	     TP_PROTO(const u8 *bRequest),
	     TP_ARGS(bRequest));

DECLARE_HOOK(android_vh_usb_configfs_pre_disconnect,
	     TP_PROTO(struct usb_composite_dev *cdev),
	     TP_ARGS(cdev));

DECLARE_HOOK(android_vh_usb_configfs_adev_create,
	     TP_PROTO(struct usb_composite_dev *cdev, int *ret),
	     TP_ARGS(cdev, ret));

DECLARE_HOOK(android_vh_usb_configfs_adev_destroy,
	     TP_PROTO(struct usb_composite_dev *cdev),
	     TP_ARGS(cdev));

#endif /* _TRACE_HOOK_USB_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
