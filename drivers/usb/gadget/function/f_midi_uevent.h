/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __F_MIDI_UEVENT_H
#define __F_MIDI_UEVENT_H

#ifdef CONFIG_USB_CONFIGFS_UEVENT
#include <linux/device.h>
#include <linux/spinlock.h>

struct f_midi_uevent_opts {
	struct device *dev;
	int card_number;
	unsigned int rmidi_device;
	bool configured;
	spinlock_t lock;
};

/**
 * set_midi_device_info - used to update the internal data of
 * f_midi_uevent_opts with the necessary data to pass to userspace.
 * @opts: contextual data for the f_midi_uevent library
 * @card_number: the number field of the struct snd_card object created
 * by the f_midi driver
 * @rmidi_device: the device field of the struct snd_rawmidi object configured
 * by the f_midi driver
 *
 * This function should be called in the f_midi driver after creating the
 * necessary snd_ objects.
 */
int set_midi_device_info(struct f_midi_uevent_opts *opts, int card_number,
	       unsigned int rmidi_device);

/**
 * clear_midi_device_info - used to clear the internal data of
 * f_midi_uevent_opts when the related objects are being removed.
 * @opts: contextual data for the f_midi_uevent library
 *
 * This function should be called in the f_midi driver prior to removing the
 * necessary snd_card and snd_rawmidi objects. May be called several times in
 * the cleanup process due to separate lifecycles for the snd_card and
 * snd_rawmidi objects, however, both objects must exist for userspace, so if
 * one goes away, clear both.
 */
void clear_midi_device_info(struct f_midi_uevent_opts *opts);

/**
 * create_midi_device - performs the necessary initialization for the
 * f_midi_uevent library and registers a f_midi device with the android_usb
 * class.
 * @opts: contextual data for the f_midi_uevent library
 */
int create_midi_device(struct f_midi_uevent_opts *opts);

#else
struct f_midi_uevent_opts {};

static inline int set_midi_device_info(struct f_midi_uevent_opts *opts,
		int card_number, unsigned int rmidi_device)
{
	return 0;
}

static inline void clear_midi_device_info(void);

static inline int create_midi_device(struct f_midi_uevent_opts *opts)
{
	return 0;
}
#endif /* CONFIG_USB_CONFIGFS_UEVENT */
#endif /* __F_MIDI_UEVENT_H */
