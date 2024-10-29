// SPDX-License-Identifier: GPL-2.0-only

#include <linux/export.h>
#include <linux/cc_platform.h>

bool noinstr cc_platform_has(enum cc_attr attr)
{
	return false;
}
EXPORT_SYMBOL_GPL(cc_platform_has);
