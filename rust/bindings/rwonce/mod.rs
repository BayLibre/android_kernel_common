// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

/// Generic implementations.
///
/// Use statements that mention the item by name take precedence over glob imports.
mod generic;
use self::generic::*;

#[cfg(target_arch = "aarch64")]
mod arm64;
#[cfg(target_arch = "aarch64")]
use self::arm64::__READ_ONCE;

/// Read a value once.
///
/// # Safety
///
/// Same requirements as `READ_ONCE` from C.
#[inline(always)]
pub unsafe fn READ_ONCE<T: Copy>(ptr: *const T) -> T {
    unsafe { __READ_ONCE(ptr) }
}

/// Write a value once.
///
/// # Safety
///
/// Same requirements as `WRITE_ONCE` from C.
#[inline(always)]
pub unsafe fn WRITE_ONCE<T: Copy>(ptr: *mut T, val: T) {
    unsafe { __WRITE_ONCE(ptr, val) }
}
