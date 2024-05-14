// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 - Google Inc
 * Author: Sebastian Ene <sebastianene@google.com>
 * Simple module for pKVM guest SMC handling.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <asm/kvm_pkvm_module.h>


static unsigned long pkvm_module_token;
int kvm_nvhe_sym(pkvm_guest_smc_proxy_hyp_init)(const struct pkvm_module_ops *ops);

void kvm_nvhe_sym(pkvm_set_guest_smc_trapping_hyp_hvc)(struct user_pt_regs *regs);

static int __init guest_smc_proxy_init(void)
{
	int ret;

	ret = pkvm_load_el2_module(kvm_nvhe_sym(pkvm_guest_smc_proxy_hyp_init),
				   &pkvm_module_token);
	if (ret) {
		pr_err("Failed to register pKVM guest SMC proxy: %d\n", ret);
		return ret;
	}

	return pkvm_register_el2_mod_call(kvm_nvhe_sym(pkvm_set_guest_smc_trapping_hyp_hvc),
					  pkvm_module_token);
}

module_init(guest_smc_proxy_init);

MODULE_AUTHOR("Sebastian Ene <sebastianene@google.com>");
MODULE_DESCRIPTION("pKVM Guest SMC proxy");
MODULE_LICENSE("GPL v2");
