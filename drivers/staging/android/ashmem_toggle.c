// SPDX-License-Identifier: GPL-2.0-only
/* ashmem_toggle.c
 *
 * This file contains the logic for toggling the behavior of Rust ashmem at
 * runtime.
 *
 * Copyright (C) 2024 Google LLC.
 */

#include <linux/moduleparam.h>
#include <linux/kconfig.h>
#include <linux/string.h>

#include "ashmem_toggle.h"

#ifdef MODULE_PARAM_PREFIX
#undef MODULE_PARAM_PREFIX
#endif
#define MODULE_PARAM_PREFIX "ashmem."

static bool ashmem_loaded;
static int unpin_behavior = ASHMEM_UNPIN_SHRINKER;

void ashmem_on_driver_loaded(void)
{
	WRITE_ONCE(ashmem_loaded, true);
}

bool ashmem_needs_shrinker(void)
{
	return READ_ONCE(unpin_behavior) == ASHMEM_UNPIN_SHRINKER;
}

static int ashmem_unpin_set(const char *buffer, const struct kernel_param *kp)
{
	int set;

	if (!strcmp(buffer, "shrinker"))
		set = ASHMEM_UNPIN_SHRINKER;
	if (!strcmp(buffer, "ignore"))
		set = ASHMEM_UNPIN_IGNORE;
	else
		return -EINVAL;

	WRITE_ONCE(unpin_behavior, set);

	if (READ_ONCE(ashmem_loaded))
		ashmem_reload_shrinker();

	return 0;
}

static int ashmem_unpin_get(char *buffer, const struct kernel_param *kp)
{
	int get = READ_ONCE(unpin_behavior);

	// The buffer is 4k bytes, so this will not overflow.
	if (get == ASHMEM_UNPIN_SHRINKER)
		strscpy(buffer, "shrinker\n", 4096);
	else if (get == ASHMEM_UNPIN_IGNORE)
		strscpy(buffer, "ignore\n", 4096);
	else
		strscpy(buffer, "\n", 4096);

	return strlen(buffer);
}

static const struct kernel_param_ops ashmem_unpin_ops = {
	.set = ashmem_unpin_set,
	.get = ashmem_unpin_get,
};

module_param_cb(unpin, &ashmem_unpin_ops, NULL, 0444);
