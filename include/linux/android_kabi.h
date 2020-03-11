/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * android_kabi.h - Android kernel abi abstraction header
 *
 * Copyright (C) 2020 Google, Inc.
 *
 * Heavily influenced by rh_kabi.h which came from the RHEL/CENTOS kernel and
 * was:
 *	Copyright (c) 2014 Don Zickus
 *	Copyright (c) 2015-2018 Jiri Benc
 *	Copyright (c) 2015 Sabrina Dubroca, Hannes Frederic Sowa
 *	Copyright (c) 2016-2018 Prarit Bhargava
 *	Copyright (c) 2017 Paolo Abeni, Larry Woodman
 *
 * These macros are to be used to try to help alivate future kernel abi changes
 * that will occur as LTS and other kernel patches are merged into the tree
 * during a period in which the kernel abi is wishing to not be disturbed.
 *
 * There are two times these macros should be used:
 *  - Before the kernel abi is "frozen"
 *    Padding can be added to various kernel structures that have in the past
 *    been known to change over time.  That will give "room" in the structure
 *    that can then be used when fields are added so that the structure size
 *    will not change.
 *
 *  - After the kernel abi is "frozen"
 *    If a structure's field is changed to a type that is identical in size to
 *    the previous type, it can be changed with a union macro
 *    If a field is added to a structure, the padding fields can be used to add
 *    the new field in a "safe" way.
 */
#ifndef _ANDROID_KABI_H
#define _ANDROID_KABI_H

#include <linux/compiler.h>
#include <linux/stringify.h>

/* Enable this variable if the ABI is now frozen */
// #define ANDROID_ABI_FROZEN


#ifdef ANDROID_ABI_FROZEN
#define _ANDROID_KABI_RESERVE(n)		u64 android_kabi_reserved##n

#else

#define _ANDROID_KABI_RESERVE(n)
#endif	/* ANDROID_ABI_FROZEN */

/*
 * Macros to use _before_ the ABI is frozen
 */
/* Reserve some "padding" in a structure for potential future use */
#define ANDROID_KABI_RESERVE(n)		_ANDROID_KABI_RESERVE(n);


/*
 * Macros to use _after_ the ABI is frozen
 */


#endif /* _ANDROID_KABI_H */
