/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#ifndef _ANDROID_DEBUG_SYMBOLS_H
#define _ANDROID_DEBUG_SYMBOLS_H

enum android_debug_symbol {
	ADS_SDATA = 0,
	ADS_BSS_END,
	ADS_PER_CPU_START,
	ADS_PER_CPU_END,
	ADS_START_RO_AFTER_INIT,
	ADS_END_RO_AFTER_INIT,
	ADS_LINUX_BANNER,
	ADS_IRQ_STACK_PTR,
	ADS_END
};

#ifdef CONFIG_ANDROID_DEBUG_SYMBOLS

void *android_debug_symbol(enum android_debug_symbol symbol);

#else /* !CONFIG_ANDROID_DEBUG_SYMBOLS */

static inline void *android_debug_symbol(enum android_debug_symbol symbol)
{
	return NULL;
}
#endif /* CONFIG_ANDROID_DEBUG_SYMBOLS */

#endif /* _ANDROID_DEBUG_SYMBOLS_H */
