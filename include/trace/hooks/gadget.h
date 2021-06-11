/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM gadget
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_GADGET_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_GADGET_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

struct usb_composite_dev;
struct usb_ctrlrequest;

DECLARE_HOOK(android_vh_ucar_ncm_ctrlrequest,
	TP_PROTO(struct usb_composite_dev *cdev, const struct usb_ctrlrequest *c, int *value),
	TP_ARGS(cdev, c, value));

#endif /* _TRACE_HOOK_GADGET_H */
/* This part must be outside protection */
#include <trace/define_trace.h>

