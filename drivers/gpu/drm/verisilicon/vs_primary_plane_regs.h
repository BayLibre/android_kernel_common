/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Icenowy Zheng <uwu@icenowy.me>
 *
 * Based on vs_dc_hw.h, which is:
 *   Copyright (C) 2023 VeriSilicon Holdings Co., Ltd.
 */

#ifndef _VS_PRIMARY_PLANE_REGS_H_
#define _VS_PRIMARY_PLANE_REGS_H_

#include <linux/bits.h>

#define VSDC_FB_ADDRESS(n)			(0x1400 + 0x4 * (n))

#define VSDC_FB_STRIDE(n)			(0x1408 + 0x4 * (n))

#define VSDC_FB_CONFIG(n)			(0x1518 + 0x4 * (n))
#define VSDC_FB_CONFIG_CLEAR_EN			BIT(8)
#define VSDC_FB_CONFIG_ROT_MASK			GENMASK(13, 11)
#define VSDC_FB_CONFIG_ROT(v)			((v) << 11)
#define VSDC_FB_CONFIG_YUV_SPACE_MASK		GENMASK(16, 14)
#define VSDC_FB_CONFIG_YUV_SPACE(v)		((v) << 14)
#define VSDC_FB_CONFIG_TILE_MODE_MASK		GENMASK(21, 17)
#define VSDC_FB_CONFIG_TILE_MODE(v)		((v) << 14)
#define VSDC_FB_CONFIG_SCALE_EN			BIT(22)
#define VSDC_FB_CONFIG_SWIZZLE_MASK		GENMASK(24, 23)
#define VSDC_FB_CONFIG_SWIZZLE(v)		((v) << 23)
#define VSDC_FB_CONFIG_UV_SWIZZLE_EN		BIT(25)
#define VSDC_FB_CONFIG_FMT_MASK			GENMASK(31, 26)
#define VSDC_FB_CONFIG_FMT(v)			((v) << 26)

#define VSDC_FB_SIZE(n)				(0x1810 + 0x4 * (n))
/* Fill with value generated with VSDC_MAKE_PLANE_SIZE(w, h) */

#define VSDC_FB_CONFIG_EX(n)			(0x1CC0 + 0x4 * (n))
#define VSDC_FB_CONFIG_EX_COMMIT		BIT(12)
#define VSDC_FB_CONFIG_EX_FB_EN			BIT(13)
#define VSDC_FB_CONFIG_EX_ZPOS_MASK		GENMASK(18, 16)
#define VSDC_FB_CONFIG_EX_ZPOS(v)		((v) << 16)
#define VSDC_FB_CONFIG_EX_DISPLAY_ID_MASK	GENMASK(19, 19)
#define VSDC_FB_CONFIG_EX_DISPLAY_ID(v)		((v) << 19)

#define VSDC_FB_TOP_LEFT(n)			(0x24D8 + 0x4 * (n))
/* Fill with value generated with VSDC_MAKE_PLANE_POS(x, y) */

#define VSDC_FB_BOTTOM_RIGHT(n)			(0x24E0 + 0x4 * (n))
/* Fill with value generated with VSDC_MAKE_PLANE_POS(x, y) */

#define VSDC_FB_SRC_GLOBAL_COLOR(n)		(0x2500 + 0x4 * (n))
#define VSDC_FB_DST_GLOBAL_COLOR(n)		(0x2508 + 0x4 * (n))
/* Alpha in the top byte, matching zhihesdk's vs_dc_hw.c (alpha << 24) */
#define VSDC_MAKE_GLOBAL_COLOR_ALPHA(a)		((u32)(a) << 24)

#define VSDC_FB_BLEND_CONFIG(n)			(0x2510 + 0x4 * (n))
#define VSDC_FB_BLEND_CONFIG_BLEND_DISABLE	BIT(1)
/*
 * BIT(1) alone ("disable") bypasses the whole compositing path rather than
 * showing the layer opaquely - confirmed via live register diff against a
 * working zhihesdk buildroot boot, whose vs_dc_hw.c never uses BIT(1) at
 * all and instead hardcodes this specific packed value (its own
 * "BLEND_PREMULTI" case) for a normal opaque/premultiplied-alpha layer.
 * Needed together with VSDC_FB_SRC/DST_GLOBAL_COLOR above (also never set
 * by this driver before, leaving global alpha at its reset value of 0 -
 * i.e. fully transparent, hiding the layer regardless of its own pixel
 * data) for the plane to actually be visible in the composited output.
 */
#define VSDC_FB_BLEND_CONFIG_PREMULTI		0x3450

#endif /* _VS_PRIMARY_PLANE_REGS_H_ */
