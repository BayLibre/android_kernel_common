// SPDX-License-Identifier: GPL-2.0

//! Task work queues.
//!
//! C header: [`include/linux/task_work.h`](../../../../include/linux/task_work.h)

use crate::bindings;
use alloc::boxed::Box;
use core::alloc::AllocError;
use core::mem::MaybeUninit;

/// A task work entry that can be submitted for execution.
pub struct TaskWorkEntry<T> {
    inner: Box<EntryInner<T>>,
}

#[repr(C)]
struct EntryInner<T> {
    twork: MaybeUninit<bindings::callback_head>,
    data: T,
}

/// This trait specifies how a task work entry should be executed.
pub trait TaskWork {
    /// The method that is called when the task work is executed.
    fn run_task_work(&mut self);
}

impl<T: TaskWork> TaskWorkEntry<T> {
    /// Tries to allocate memory to hold a task work entry.
    pub fn new(value: T) -> Result<Self, AllocError> {
        Ok(Self {
            inner: Box::try_new(EntryInner {
                twork: MaybeUninit::uninit(),
                data: value,
            })?,
        })
    }

    /// Access the inner value immutably.
    pub fn get(&self) -> &T {
        &self.inner.data
    }

    /// Access the inner value mutably.
    pub fn get_mut(&mut self) -> &mut T {
        &mut self.inner.data
    }

    /// Submit this task work entry for execution.
    pub fn submit_twa_resume(self) {
        // SAFETY: Since EntryInner is `#[repr(C)]`, casting the pointers gives a pointer to
        // the `twork` field.
        let inner = Box::into_raw(self.inner) as *mut bindings::callback_head;
        // SAFETY: The `run_task_work` method has the correct generic parameter, so it will not
        // misbehave when called with `inner`.
        unsafe {
            bindings::init_task_work(inner, Some(run_task_work::<T>));
            bindings::task_work_add(
                bindings::get_current(),
                inner,
                bindings::task_work_notify_mode_TWA_RESUME,
            );
        }
    }
}

unsafe extern "C" fn run_task_work<T: TaskWork>(inner: *mut bindings::callback_head) {
    // SAFETY: We know that `submit_twa_resume` gave us a pointer that originates from a Box.
    unsafe {
        let mut entry = Box::from_raw(inner as *mut EntryInner<T>);
        entry.data.run_task_work();
    }
}
