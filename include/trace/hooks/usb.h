/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM usb

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_USB_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_USB_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

struct usb_function_instance;
struct usb_ctrlrequest;
struct dwc3;
union dwc3_event;

DECLARE_HOOK(android_vh_config_usb_cfg_link,
	TP_PROTO(struct usb_function_instance *fi, int ret),
	TP_ARGS(fi, ret));
DECLARE_HOOK(android_vh_config_usb_cfg_unlink,
	TP_PROTO(struct usb_function_instance *fi),
	TP_ARGS(fi));
DECLARE_HOOK(android_vh_dwc3_process_event_entry,
	TP_PROTO(struct dwc3 *dwc, const union dwc3_event *event),
	TP_ARGS(dwc, event));
DECLARE_HOOK(android_vh_dwc3_gadget_pullup,
	TP_PROTO(struct dwc3 *dwc, int is_on, int ret),
	TP_ARGS(dwc, is_on, ret));
DECLARE_HOOK(android_vh_dwc3_ep0_inspect_setup,
	TP_PROTO(struct dwc3 *dwc, struct usb_ctrlrequest *ctrl, int ret),
	TP_ARGS(dwc, ctrl, ret));
DECLARE_HOOK(android_vh_dwc3_ep0_xfer_complete,
	TP_PROTO(struct dwc3 *dwc),
	TP_ARGS(dwc));

/* macro versions of hooks are no longer required */

#endif /* _TRACE_HOOK_USB_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
