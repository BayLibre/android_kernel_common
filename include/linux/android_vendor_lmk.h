// SPDX-License-Identifier: GPL-2.0
/*
 * Android Vendor Low Memory Killer Support
 *
 * Copyright 2025 Google LLC
 */

#ifndef _ANDROID_VENDOR_LMK_H
#define _ANDROID_VENDOR_LMK_H

#ifdef CONFIG_ANDROID_VENDOR_LMK_EVENT
int android_trigger_vendor_lmk_kill(int reason, short min_oom_score_adj);
#else
static inline int android_trigger_vendor_lmk_kill(int reason, short min_oom_score_adj)
{
	return -ENOSYS;
}
#endif

#endif /* _ANDROID_VENDOR_LMK_H */

