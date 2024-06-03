// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

use kernel::{bindings::task_struct, task::Task, tracepoint::declare_trace};

declare_trace! {
    fn android_vh_rust_binder_set_priority(is_oneway: bool, task: *mut task_struct);
    fn android_vh_rust_binder_restore_priority(task: *mut task_struct);
}

#[inline]
pub(crate) fn trace_set_priority(is_oneway: bool, task: &Task) {
    // SAFETY: The pointer to `task` is valid.
    unsafe { android_vh_rust_binder_set_priority(is_oneway, task.as_raw()) }
}

#[inline]
pub(crate) fn trace_restore_priority(task: &Task) {
    // SAFETY: The pointer to `task` is valid.
    unsafe { android_vh_rust_binder_restore_priority(task.as_raw()) }
}
