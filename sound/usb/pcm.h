/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __USBAUDIO_PCM_H
#define __USBAUDIO_PCM_H

void snd_usb_set_pcm_ops(struct snd_pcm *pcm, int stream);
int snd_usb_pcm_suspend(struct snd_usb_stream *as);
int snd_usb_pcm_resume(struct snd_usb_stream *as);

int snd_usb_init_pitch(struct snd_usb_audio *chip,
		       const struct audioformat *fmt);
void snd_usb_preallocate_buffer(struct snd_usb_substream *subs);

int snd_usb_audioformat_set_sync_ep(struct snd_usb_audio *chip,
				    struct audioformat *fmt);

void close_endpoints(struct snd_usb_audio *chip,
			    struct snd_usb_substream *subs);
int configure_endpoints(struct snd_usb_audio *chip,
			       struct snd_usb_substream *subs);

const struct audioformat *
find_format(struct list_head *fmt_list_head, snd_pcm_format_t format,
	    unsigned int rate, unsigned int channels, bool strict_match,
	    struct snd_usb_substream *subs);
const struct audioformat *
find_substream_format(struct snd_usb_substream *subs,
		      const struct snd_pcm_hw_params *params);
#endif /* __USBAUDIO_PCM_H */
