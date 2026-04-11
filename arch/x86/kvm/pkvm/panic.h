/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PKVM_X86_PANIC_H
#define __PKVM_X86_PANIC_H

#include <asm/ptrace.h>

void __noreturn pkvm_panic(const char *fmt, ...);

#endif /* __PKVM_X86_PANIC_H */
