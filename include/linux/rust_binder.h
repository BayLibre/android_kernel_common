/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_RUST_BINDER_H
#define _LINUX_RUST_BINDER_H

#include <linux/seq_file.h>
#include <uapi/linux/android/binder.h>
#include <uapi/linux/android/binderfs.h>

/*
 * This symbol is exposed by `rust_binderfs.c` and exists here so that Rust
 * Binder can call it. All other symbols are exported by the Rust Binder driver.
 */
int init_rust_binderfs(void);

/*
 * The internal data types in the Rust Binder driver are opaque to C, so we use
 * void pointer typedefs for these types.
 */
typedef void *rust_binder_device;

int rust_binder_stats_show(struct seq_file *m, void *unused);
DEFINE_SHOW_ATTRIBUTE(rust_binder_stats);

int rust_binder_state_show(struct seq_file *m, void *unused);
DEFINE_SHOW_ATTRIBUTE(rust_binder_state);

int rust_binder_transactions_show(struct seq_file *m, void *unused);
DEFINE_SHOW_ATTRIBUTE(rust_binder_transactions);

int rust_binder_transaction_log_show(struct seq_file *m, void *unused);
DEFINE_SHOW_ATTRIBUTE(rust_binder_transaction_log);

extern const struct file_operations rust_binder_fops;
rust_binder_device rust_binder_new_device(char *name);
void rust_binder_remove_device(rust_binder_device device);

#endif
