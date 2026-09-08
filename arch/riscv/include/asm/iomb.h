/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Zhihe Computing Limited.
 *
 * A210 write-combine buffer drain, used to implement dma_mb()/dma_wmb()
 * for CONFIG_ARCH_ZHIHE. See arch/riscv/kernel/iomb.c.
 */

#ifndef A210_IOMB_H
#define A210_IOMB_H

inline void a210_iomb(void);
inline void a210_iowmb(void);

#endif
