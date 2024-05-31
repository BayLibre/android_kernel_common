// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

#[cfg(not(CONFIG_LTO))]
pub(super) use super::generic::__READ_ONCE;

// When building with LTO, there is an increased risk of the compiler
// converting an address dependency headed by a READ_ONCE() invocation
// into a control dependency and consequently allowing for harmful
// reordering by the CPU.
//
// Ensure that such transformations are harmless by overriding the generic
// READ_ONCE() definition with one that provides RCpc acquire semantics
// when building with LTO.
#[cfg(CONFIG_LTO)]
#[inline(always)]
pub(super) unsafe fn __READ_ONCE<T>(ptr: *const T) -> T {
    use core::sync::atomic::*;

    macro_rules! load {
        ($ptr:expr, $atomic:ident) => {{
            let ptr = $ptr as *const $atomic;
            // TODO: Use inline assembly instead.
            let value = (&*ptr).load(Ordering::Acquire);
            core::mem::transmute_copy::<_, T>(&value)
        }};
    }
    
    unsafe {
        match core::mem::size_of::<T>() {
            1 => load!(ptr, AtomicU8),
            2 => load!(ptr, AtomicU16),
            4 => load!(ptr, AtomicU32),
            8 => load!(ptr, AtomicU64),
            _ => core::ptr::read_volatile(ptr)
        }
    }
}
