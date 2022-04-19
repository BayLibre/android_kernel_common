/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM audio_usboffload

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_AUDIO_USBOFFLOAD_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_AUDIO_USBOFFLOAD_H

#include <trace/hooks/vendor_hooks.h>

struct usb_interface;
struct snd_usb_audio;
struct usb_device;
struct audioformat;

DECLARE_HOOK(android_vh_audio_usb_offload_vendor_set,
	TP_PROTO(void *arg),
	TP_ARGS(arg));

DECLARE_HOOK(android_vh_audio_usb_offload_ep_action,
	TP_PROTO(void *arg, bool action),
	TP_ARGS(arg, action));

DECLARE_HOOK(android_vh_audio_usb_offload_synctype,
	TP_PROTO(void *arg, int attr, bool *need_ignore),
	TP_ARGS(arg, attr, need_ignore));

DECLARE_HOOK(android_vh_audio_usb_offload_connect,
	TP_PROTO(struct usb_interface *intf, struct snd_usb_audio *chip),
	TP_ARGS(intf, chip));

DECLARE_RESTRICTED_HOOK(android_rvh_audio_usb_offload_disconnect,
	TP_PROTO(struct usb_interface *intf),
	TP_ARGS(intf), 1);

DECLARE_HOOK(android_vh_audio_usb_offload_set_rate,
	TP_PROTO(int iface, int rate, int alt, int *ret),
	TP_ARGS(iface, rate, alt, ret));

DECLARE_HOOK(android_vh_audio_usb_offload_pcmbuf,
	TP_PROTO(struct usb_device *dev, int iface, int *ret),
	TP_ARGS(dev, iface, ret));

DECLARE_RESTRICTED_HOOK(android_rvh_audio_usb_offload_pcm_intf,
	TP_PROTO(struct snd_usb_audio *chip, int iface, int alt, int direction),
	TP_ARGS(chip, iface, alt, direction), 1);

DECLARE_HOOK(android_vh_audio_usb_offload_pcm_binterval,
	TP_PROTO(const struct audioformat *fp, const struct audioformat *found,
		int *cur_attr, int *attr),
	TP_ARGS(fp, found, cur_attr, attr));

DECLARE_RESTRICTED_HOOK(android_rvh_audio_usb_offload_pcm_control,
	TP_PROTO(struct usb_device *udev, int onoff, int direction, int *ret),
	TP_ARGS(udev, onoff, direction, ret), 1);

#endif /* _TRACE_HOOK_AUDIO_USBOFFLOAD_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
