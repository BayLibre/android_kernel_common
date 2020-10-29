/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#ifndef _ANDROID_DEBUG_SYMBOLS_H
#define _ANDROID_DEBUG_SYMBOLS_H

enum android_debug_symbol {
	ADS_SDATA,
	ADS_BSS_END,
	ADS_PERCPU_START,
	ADS_PERCPU_END
};

#ifdef CONFIG_ANDROID_DEBUG_SYMBOLS

void *get_android_debug_symbol(enum android_debug_symbol symbol);

#else /* !CONFIG_ANDROID_DEBUG_SYMBOLS */

static inline void *get_android_debug_symbol(enum android_debug_symbol symbol)
{
	return NULL;
}

#endif /* _ANDROID_DEBUG_SYMBOLS_H */
