/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM usb

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_USB_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_USB_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

union dwc3_event;
struct dwc3;

DECLARE_HOOK(android_vh_dwc3_process_event_entry,
	TP_PROTO(struct dwc3 *dwc, const union dwc3_event *event),
	TP_ARGS(dwc, event));

/* macro versions of hooks are no longer required */

#endif /* _TRACE_HOOK_USB_H */
/* This part must be outside protection */
#include <trace/define_trace.h>

