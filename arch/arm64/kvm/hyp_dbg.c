// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 - Google LLC
 * Author: Sebastian Ene <sebastianene@google.com>
 */

#include <linux/export.h>
#include <linux/kvm_host.h>
#include <asm/kvm_asm.h>

void __noreturn hyp_panic(void);

void __kvm_nvhe_hyp_debug_service_testpoint(void)
{
	hyp_panic();
}

void hyp_debug_testpoint(void)
{
	kvm_call_hyp_nvhe(__host_debug_hyp_panic);
}
EXPORT_SYMBOL_GPL(hyp_debug_testpoint);
