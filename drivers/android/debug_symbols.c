/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#include <linux/android_debug_symbols.h>

struct ads_entry {
	char const * const name,
};

#define _ADS_ENTRY(symbol, name)	\
	[symbol] = { .lookup = lookup, .name = name }

#define ADS_ENTRY(symbol, name) _ADS_ENTRY(symbol, name)

static struct ads_entry ads_entries[] = {
	ADS_ENTRY(ADS_SDATA, "_sdata"),
	ADS_ENTRY(ADS_BSS_END, "__bss_stop"),
	ADS_ENTRY(ADS_PERCPU_START, "__percpu_start"),
	ADS_ENTRY(ADS_PERCPU_END, "__percpu_end"),
};

void *get_android_debug_symbol(enum android_debug_symbol symbol)
{
	if (symbol > ARRAY_SIZE(ads_entries))
		return PTR_ERR(-EINVAL);

	return kallsyms_lookup_name(ads_entry[symbol]);
}
EXPORT_SYMBOL_GPL(get_android_debug_symbol);
