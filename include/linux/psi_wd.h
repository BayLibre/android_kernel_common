// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Google, Inc.
 */

#ifndef _ANDROID_PSI_WD_H
#define _ANDROID_PSI_WD_H

#ifdef CONFIG_ANDROID_PSI_WATCHDOG
extern void psi_wd_start(struct psi_trigger *t);
extern void psi_wd_stop(struct task_struct *task, struct psi_trigger *t);
#else
inline void psi_wd_start(struct psi_trigger *) {}
inline void psi_wd_stop(struct task_struct *, struct psi_trigger *) {}
#endif

#endif /* _ANDROID_PSI_WD_H */
