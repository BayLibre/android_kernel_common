/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Zhihe Audio PCM support
 *
 * Copyright (C) 2024 Zhihe Computing Technology (Shenzhen) Co., Ltd..
 *
 * Author: Anonymous <anonymous@zhcomputing.com>
 *
 * Ported from the Zhihe A210 SDK.
 */

#ifndef _ZHIHE_PCM_H
#define _ZHIHE_PCM_H

#include <linux/types.h>

struct platform_device;

int zhihe_pcm_dma_init(struct platform_device *pdev, size_t size);

#endif /* _ZHIHE_PCM_H */
