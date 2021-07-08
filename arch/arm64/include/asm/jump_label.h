/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 Huawei Ltd.
 * Author: Jiang Liu <liuj97@gmail.com>
 *
 * Based on arch/arm/include/asm/jump_label.h
 */
#ifndef __ASM_JUMP_LABEL_H
#define __ASM_JUMP_LABEL_H

#ifndef __ASSEMBLY__

#include <linux/types.h>
#include <asm/insn.h>

#define JUMP_LABEL_NOP_SIZE		AARCH64_INSN_SIZE

static __always_inline bool arch_static_branch(struct static_key *key,
					       bool branch)
{
#ifdef BUILD_FIPS140_KO
	/*
	 * The fips140 module doesn't support jump labels, as they would
	 * invalidate the hash of the .text section.  So we must override them
	 * with regular branches.
	 *
	 * arch_static_branch{,_jump}() must return "was the branch taken?".
	 * The 'branch' argument is true if the branch is taken in the
	 * "disabled" case.  So the correct logic is (enabled && !branch) ||
	 * (!enabled && branch), a.k.a. enabled ^ branch.
	 *
	 * This should use atomic_read(&key->enabled), but atomic.h indirectly
	 * includes this header.  So we have to use READ_ONCE() directly.
	 */
	return (bool)READ_ONCE(key->enabled.counter) ^ branch;
#else
	asm_volatile_goto(
		"1:	nop					\n\t"
		 "	.pushsection	__jump_table, \"aw\"	\n\t"
		 "	.align		3			\n\t"
		 "	.long		1b - ., %l[l_yes] - .	\n\t"
		 "	.quad		%c0 - .			\n\t"
		 "	.popsection				\n\t"
		 :  :  "i"(&((char *)key)[branch]) :  : l_yes);

	return false;
l_yes:
	return true;
#endif /* !BUILD_FIPS140_KO */
}

static __always_inline bool arch_static_branch_jump(struct static_key *key,
						    bool branch)
{
#ifdef BUILD_FIPS140_KO
	return arch_static_branch(key, branch);
#else
	asm_volatile_goto(
		"1:	b		%l[l_yes]		\n\t"
		 "	.pushsection	__jump_table, \"aw\"	\n\t"
		 "	.align		3			\n\t"
		 "	.long		1b - ., %l[l_yes] - .	\n\t"
		 "	.quad		%c0 - .			\n\t"
		 "	.popsection				\n\t"
		 :  :  "i"(&((char *)key)[branch]) :  : l_yes);

	return false;
l_yes:
	return true;
#endif /* !BUILD_FIPS140_KO */
}

#endif  /* __ASSEMBLY__ */
#endif	/* __ASM_JUMP_LABEL_H */
