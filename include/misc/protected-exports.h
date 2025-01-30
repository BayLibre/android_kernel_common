/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Declares a list of symbols that are protected from being exported by
 * unsigned modules.
 *
 * Copyright (C) 2025 Siddharth Nayyar <sidnayyar@google.com>
 */

#ifndef __MISC_PROTECTED_EXPORTS_H__
#define __MISC_PROTECTED_EXPORTS_H__

int _nr_protected_exports_symbols(void);

#ifdef CONFIG_MODULE_SIG_PROTECT
#define NR_PROTECTED_EXPORTS_SYMBOLS 0
#else
#define NR_PROTECTED_EXPORTS_SYMBOLS _nr_protected_exports_symbols()
#endif

const char *const gki_protected_exports_symbols[];

#endif /* __MISC_PROTECTED_EXPORTS_H__ */
