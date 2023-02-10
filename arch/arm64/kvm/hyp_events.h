/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ARM64_KVM_HYP_EVENTS_H__
#define __ARM64_KVM_HYP_EVENTS_H__

#ifdef CONFIG_TRACING
int kvm_hyp_init_events(void);
void kvm_hyp_init_events_tracefs(struct dentry *parent);
#else
static inline int kvm_hyp_init_events(void) { return 0; }
static inline void kvm_hyp_init_events_tracefs(struct dentry *parent) { }
#endif
#endif
