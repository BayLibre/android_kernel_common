// SPDX-License-Identifier: GPL-2.0
/* vendor_lmk.c
 *
 * Android Vendor LMK Support
 *
 * Copyright 2025 Google LLC
 */

#include <linux/module.h>
#include <linux/oom.h>

#define CREATE_TRACE_POINTS
#include "vendor_lmk_trace.h"

#define MAX_REASON_NUM 1000

int android_trigger_vendor_lmk_kill(int reason, short min_oom_score_adj)
{
	if (reason < 0 || reason >= MAX_REASON_NUM)
		return -EINVAL;

	if (min_oom_score_adj < OOM_SCORE_ADJ_MIN ||
		min_oom_score_adj > OOM_SCORE_ADJ_MAX)
		return -EINVAL;

	trace_trigger_vendor_lmk_kill(reason, min_oom_score_adj);
	return 0;
}
EXPORT_SYMBOL_GPL(android_trigger_vendor_lmk_kill);

MODULE_LICENSE("GPL");

