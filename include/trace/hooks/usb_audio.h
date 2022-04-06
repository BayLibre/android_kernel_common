/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM usb_audio

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_USB_AUDIO_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_USB_AUDIO_H

#include <trace/hooks/vendor_hooks.h>

struct usb_interface;
struct snd_usb_audio;

DECLARE_HOOK(android_vh_usb_audio_connect,
	TP_PROTO(struct usb_interface *intf),
	TP_ARGS(intf));

DECLARE_HOOK(android_vh_usb_audio_add_ctls,
	TP_PROTO(struct usb_interface *intf, struct snd_usb_audio *chip),
	TP_ARGS(intf, chip));

DECLARE_HOOK(android_vh_usb_audio_disconnect,
	TP_PROTO(struct usb_interface *intf),
	TP_ARGS(intf));

#endif /* _TRACE_HOOK_USB_AUDIO_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
