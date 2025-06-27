/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>
#include <linux/objtool.h>
#include <asm/unwind_hints.h>
#include <asm/segment.h>
#include <asm/cache.h>
#include <asm/cpufeatures.h>
#include <asm/nospec-branch.h>

.align L1_CACHE_BYTES, 0xcc
SYM_CODE_START_NOALIGN(mds_verw_sel)
	UNWIND_HINT_UNDEFINED
	ANNOTATE_NOENDBR
	.word __KERNEL_DS
.align L1_CACHE_BYTES, 0xcc
SYM_CODE_END(mds_verw_sel);

#ifdef CONFIG_X86_64
/*
 * This sequence executes branches in order to remove user branch information
 * from the branch history tracker in the Branch Predictor, therefore removing
 * user influence on subsequent BTB lookups.
 *
 * It should be used on parts prior to Alder Lake. Newer parts should use the
 * BHI_DIS_S hardware control instead. If a pre-Alder Lake part is being
 * virtualized on newer hardware the VMM should protect against BHI attacks by
 * setting BHI_DIS_S for the guests.
 *
 * CALLs/RETs are necessary to prevent Loop Stream Detector(LSD) from engaging
 * and not clearing the branch history. The call tree looks like:
 *
 * call 1
 *    call 2
 *      call 2
 *        call 2
 *          call 2
 * 	      call 2
 * 	      ret
 * 	    ret
 *        ret
 *      ret
 *    ret
 * ret
 *
 * This means that the stack is non-constant and ORC can't unwind it with %rsp
 * alone.  Therefore we unconditionally set up the frame pointer, which allows
 * ORC to unwind properly.
 *
 * The alignment is for performance and not for safety, and may be safely
 * refactored in the future if needed.
 */
SYM_FUNC_START(clear_bhb_loop)
	push	%rbp
	mov	%rsp, %rbp
	movl	$5, %ecx
	ANNOTATE_INTRA_FUNCTION_CALL
	call	1f
	jmp	5f
	.align 64, 0xcc
	ANNOTATE_INTRA_FUNCTION_CALL
1:	call	2f
	RET
	.align 64, 0xcc
2:	movl	$5, %eax
3:	jmp	4f
	nop
4:	sub	$1, %eax
	jnz	3b
	sub	$1, %ecx
	jnz	1b
	RET
5:	lfence
	pop	%rbp
	RET
SYM_FUNC_END(clear_bhb_loop)
STACK_FRAME_NON_STANDARD(clear_bhb_loop)
#endif
