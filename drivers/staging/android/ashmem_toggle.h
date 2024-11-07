/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_ASHMEM_TOGGLE_H
#define _LINUX_ASHMEM_TOGGLE_H

enum {
	ASHMEM_UNPIN_SHRINKER = 0,
	ASHMEM_UNPIN_IGNORE = 1,
	ASHMEM_UNPIN_IMMEDIATELY = 2,
};

bool ashmem_needs_shrinker(void);
bool ashmem_unpin_now(void);
void ashmem_reload_shrinker(void);
void ashmem_on_driver_loaded(void);

#endif /* _LINUX_ASHMEM_TOGGLE_H */
