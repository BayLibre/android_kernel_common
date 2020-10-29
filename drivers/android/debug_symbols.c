// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/android_debug_symbols.h>

struct ads_entry {
	char const * const name;
	void *addr;
};

/*
 * This module maintains static array of symbol and address information.
 * Add all required core kernel symbols and their addresses into ads_entries[] array,
 * so that vendor modules can query and to find address of non-exported symbol.
 */
static struct ads_entry ads_entries[] = {
	{ .name = "_sdata", .addr = _sdata },
	{ .name = "__bss_stop", .addr =  __bss_stop },
	{ .name = "__per_cpu_start", .addr = __per_cpu_start },
	{ .name = "__per_cpu_end", .addr = __per_cpu_end },
	{ .name = "__start_ro_after_init", .addr = __start_ro_after_init },
	{ .name = "__end_ro_after_init", .addr = __end_ro_after_init },
	{ .name = NULL, .addr = NULL }
};

/*
 * android_debug_symbol - Provide address inforamtion of debug symbol.
 * @symbol: Index of debug symbol array.
 *
 * Return address of core kernel symbol on success and a negative errno wwill be
 * returned in error cases.
 *
 */
void *android_debug_symbol(enum android_debug_symbol symbol)
{
	if (symbol >= ADS_END)
		return ERR_PTR(-EINVAL);

	return ads_entries[symbol].addr;
}
EXPORT_SYMBOL_GPL(android_debug_symbol);
