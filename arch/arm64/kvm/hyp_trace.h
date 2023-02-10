/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ARM64_KVM_HYP_TRACE_H__
#define __ARM64_KVM_HYP_TRACE_H__

#ifdef CONFIG_TRACING
int init_hyp_tracefs(void);
#else
static inline int init_hyp_tracefs(void) { return 0; }
#endif
#endif
