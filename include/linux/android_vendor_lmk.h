// SPDX-License-Identifier: GPL-2.0

#ifndef _ANDROID_VENDOR_LMK_H
#define _ANDROID_VENDOR_LMK_H

#ifdef CONFIG_ANDROID_VENDOR_LMK_EVENT
int trigger_vendor_lmk_kill(int reason, short min_oom_score_adj);
#else
static inline int trigger_vendor_lmk_kill(int reason, short min_oom_score_adj)
{
	return -ENOSYS;
}
#endif

#endif /* _ANDROID_VENDOR_LMK_H */