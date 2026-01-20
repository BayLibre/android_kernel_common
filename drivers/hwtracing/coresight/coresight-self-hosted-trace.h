/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Arm v8 Self-Hosted trace support.
 *
 * Copyright (C) 2021 ARM Ltd.
 */

#ifndef __CORESIGHT_SELF_HOSTED_TRACE_H
#define __CORESIGHT_SELF_HOSTED_TRACE_H

#include <asm/sysreg.h>

static inline u64 read_trfcr(void)
{
	return read_sysreg_s(SYS_TRFCR_EL1);
}

static inline void write_trfcr(u64 val)
{
	write_sysreg_s(val, SYS_TRFCR_EL1);
	isb();
}

<<<<<<< HEAD   (630deefd7859319dc2e32f22d340746c94c10091 Revert "Revert "xfrm: destroy xfrm_state synchronously on ne)
static inline u64 cpu_prohibit_trace(void)
{
	u64 trfcr = read_trfcr();

	/* Prohibit tracing at EL0 & the kernel EL */
	write_trfcr(trfcr & ~(TRFCR_ELx_ExTRE | TRFCR_ELx_E0TRE));
	/* Return the original value of the TRFCR */
	return trfcr;
||||||| BASE   (cd93db1b1b4460e6ee77564024ea461e5940f69c nbd: defer config unlock in nbd_genl_connect)
=======
static inline void cpu_prohibit_trace(void)
{
	u64 trfcr = read_trfcr();

	/* Prohibit tracing at EL0 & the kernel EL */
	write_trfcr(trfcr & ~(TRFCR_ELx_ExTRE | TRFCR_ELx_E0TRE));
>>>>>>> BRANCH (d4b7290c1b5f23f41c7c1edd43b6cb289ae65de9 coresight-etm4x: add isb() before reading the TRCSTATR)
}
#endif /*  __CORESIGHT_SELF_HOSTED_TRACE_H */
