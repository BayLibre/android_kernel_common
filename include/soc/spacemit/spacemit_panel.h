/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Spacemit panel notification header
 *
 * Copyright (C) 2023 Spacemit Co., Ltd.
 */

#ifndef _SOC_SPACEMIT_PANEL_H_
#define _SOC_SPACEMIT_PANEL_H_

/* Panel notification events */
#define DRM_PANEL_EARLY_BLANK		0x01
#define DRM_PANEL_BLANK			0x02
#define DRM_PANEL_EARLY_EVENT_BLANK	0x03
#define DRM_PANEL_EVENT_BLANK		0x04

/* Panel blank types */
#define DRM_PANEL_BLANK_UNBLANK		0
#define DRM_PANEL_BLANK_POWERDOWN	1

/* HDMI notification events */
#define DRM_HDMI_EVENT_CONNECTED	0x10
#define DRM_HDMI_EVENT_DISCONNECTED	0x11

#endif /* _SOC_SPACEMIT_PANEL_H_ */
