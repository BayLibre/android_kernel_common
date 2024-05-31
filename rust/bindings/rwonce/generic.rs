// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

#[inline(always)]
pub(super) unsafe fn __READ_ONCE<T>(ptr: *const T) -> T {
    unsafe { core::ptr::read_volatile(ptr) }
}

#[inline(always)]
pub(super) unsafe fn __WRITE_ONCE<T>(ptr: *mut T, val: T) {
    unsafe { core::ptr::write_volatile(ptr, val) }
}
