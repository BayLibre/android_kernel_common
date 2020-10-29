// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/android_debug_symbols.h>
#include <asm/stacktrace.h>

struct ads_entry {
	char *name;
	void *addr;
};

/*
 * This module maintains static array of symbol and address information.
 * Add all required core kernel symbols and their addresses into ads_entries[] array,
 * so that vendor modules can query and to find address of non-exported symbol.
 */
static struct ads_entry ads_entries[ADS_END];

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

#define ADS_ADD_ENTRY(idx, symbol)			\
do {							\
	ads_entries[idx].name = #symbol;		\
	ads_entries[idx].addr = (void *)symbol;		\
} while(0)

#define ADS_ADD_PER_CPU_ENTRY(idx, symbol)		\
do {							\
	ads_entries[idx].name = #symbol;		\
	ads_entries[idx].addr = raw_cpu_ptr(symbol);	\
} while(0)

static int __init debug_symbol_init(void)
{
	ADS_ADD_ENTRY(ADS_SDATA, _sdata);
	ADS_ADD_ENTRY(ADS_BSS_END, __bss_stop);
	ADS_ADD_ENTRY(ADS_PER_CPU_START, __per_cpu_start);
	ADS_ADD_ENTRY(ADS_PER_CPU_END, __per_cpu_end);
	ADS_ADD_ENTRY(ADS_START_RO_AFTER_INIT, __start_ro_after_init);
	ADS_ADD_ENTRY(ADS_END_RO_AFTER_INIT, __end_ro_after_init);
	ADS_ADD_ENTRY(ADS_LINUX_BANNER, linux_banner);
	ADS_ADD_PER_CPU_ENTRY(ADS_IRQ_STACK_PTR, irq_stack_ptr);

	return 0;
}
module_init(debug_symbol_init);

static void __exit debug_symbol_exit(void)
{ }
module_exit(debug_symbol_exit);

MODULE_DESCRIPTION("Debug Symbol Driver");
MODULE_LICENSE("GPL v2");
