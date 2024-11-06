/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM usb
<<<<<<< HEAD   (871ceb6f212cc1380cfdce82017a75d15701a762 ANDROID: cpufreq: scmi: Fix scmi_limit_notify_cb handling)

||||||| BASE   (5dc82090ccc0ec6118c0c09565932afb7c4e2e5e FROMGIT: wifi: cfg80211: Do not create BSS entries for unsup)
=======
>>>>>>> CHANGE (1586d3cdc925ae9352529763ac1eabc69c09a15d ANDROID: USB: add vendor hook functions for USB)
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_USB_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_USB_H

#include <trace/hooks/vendor_hooks.h>

struct usb_device;

/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
DECLARE_RESTRICTED_HOOK(android_rvh_usb_dev_suspend,
	TP_PROTO(struct usb_device *udev, pm_message_t msg, bool *bypass),
	TP_ARGS(udev, msg, bypass), 1);

DECLARE_HOOK(android_vh_usb_dev_resume,
	TP_PROTO(struct usb_device *udev, pm_message_t msg, bool *bypass),
	TP_ARGS(udev, msg, bypass));

DECLARE_HOOK(android_vh_usb_new_device_added,
	TP_PROTO(struct usb_device *udev, int *err),
	TP_ARGS(udev, err));

#endif /*  _TRACE_HOOK_USB_H */
/*  This part must be outside protection */
#include <trace/define_trace.h>
