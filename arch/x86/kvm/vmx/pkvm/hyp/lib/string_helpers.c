// SPDX-License-Identifier: GPL-2.0

#include "debug.h"
#include "bug.h"

void fortify_panic(const char *name)
{
	pkvm_err("pkvm: detected buffer overflow in %s\n", name);
	PKVM_ASSERT(0);
}
