// SPDX-License-Identifier: GPL-2.0
/*
 * Zhihe ALSA Soc Audio Layer DMA init function.
 *
 * Copyright (C) 2024 Zhihe Computing Technology (Shenzhen) Co., Ltd..
 *
 * Author: Anonymous <anonymous@zhcomputing.com>
 *
 * Ported from the Zhihe A210 SDK.
 */

#include <linux/dmaengine.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm.h>

#include "zhihe-pcm.h"

static bool filter(struct dma_chan *chan, void *param)
{
	chan->private = param;

	return true;
}

static const struct snd_pcm_hardware zhihe_pcm_hardware = {
	.periods_min		= 2,
	.periods_max		= UINT_MAX,
	.period_bytes_min	= 256,
	.period_bytes_max	= 64 * 1024 * 10,
	.buffer_bytes_max	= SIZE_MAX,
	.info			= SNDRV_PCM_INFO_MMAP |
				  SNDRV_PCM_INFO_MMAP_VALID |
				  SNDRV_PCM_INFO_INTERLEAVED |
				  SNDRV_PCM_INFO_RESUME |
				  SNDRV_PCM_INFO_PAUSE,
};

static const struct snd_dmaengine_pcm_config zhihe_dmaengine_pcm_config = {
	.pcm_hardware		= &zhihe_pcm_hardware,
	.prepare_slave_config	= snd_dmaengine_pcm_prepare_slave_config,
	.compat_filter_fn	= filter,
};

int zhihe_pcm_dma_init(struct platform_device *pdev,
		       size_t size __maybe_unused)
{
	struct snd_dmaengine_pcm_config *config;

	config = devm_kmemdup(&pdev->dev, &zhihe_dmaengine_pcm_config,
			      sizeof(*config), GFP_KERNEL);
	if (!config)
		return -ENOMEM;

	return devm_snd_dmaengine_pcm_register(&pdev->dev, config,
					       SND_DMAENGINE_PCM_FLAG_COMPAT);
}
EXPORT_SYMBOL_GPL(zhihe_pcm_dma_init);
