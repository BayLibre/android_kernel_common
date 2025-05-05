// SPDX-License-Identifier: GPL-2.0

//! x86_64 page size compat
//!
//! C header: [`include/linux/page_size_compat.h`](srctree/include/linux/page_size_compat.h).
//!
//! Rust helper macros for page size emulation.

/// __page_shift is the emulated page shift.
#[macro_export]
macro_rules! __page_shift {
    () => {{
        #[cfg(CONFIG_JUMP_LABEL)]
        {
            // SAFETY: Accessing C static key
            let emulated = unsafe {
                $crate::jump_label::static_branch_unlikely!(
                    $crate::bindings::page_shift_compat_enabled,
                    $crate::bindings::static_key_false,
                    key
                )
            };

            if emulated {
                // SAFETY: Reading static mutable C variable page_shift_compat.
                let shift = unsafe { $crate::bindings::page_shift_compat };
                // page_shift_compat is sanitized and range checked by boot
                // parameter parsing; so this conversion should always be safe.
                shift.try_into().unwrap()
            } else {
                $crate::page::PAGE_SHIFT
            }
        }
        #[cfg(not(CONFIG_JUMP_LABEL))]
        {
            $crate::page::PAGE_SHIFT
        }
    }};
}

pub use __page_shift;

/// __page_size is the emulated page size.
#[macro_export]
macro_rules! __page_size {
    () => {{
        let shift: usize = $crate::__page_shift!();

        1usize << shift
    }};
}

pub use __page_size;

/// __page_mask can be used to align addresses to a __page boundary.
#[macro_export]
macro_rules! __page_mask {
    () => {{
        let page_size: usize = $crate::__page_size!();

        !(page_size - 1)
    }};
}

pub use __page_mask;

/// Aligns the given address UP to the nearest __page boundary.
#[macro_export]
macro_rules! __page_align {
    ($addr:expr) => {{
        let addr_val: usize = $addr;
        let page_size: usize = $crate::__page_size!();
        let mask: usize = $crate::__page_mask!();

        (addr_val + page_size - 1) & mask

    }};
}

pub use __page_align;

/// Aligns the given address DOWN to the nearest __page boundary.
#[macro_export]
macro_rules! __page_align_down {
    ($addr:expr) => {{
        let addr_val: usize = $addr;

        let mask: usize = $crate::__page_mask!();
        addr_val & mask
    }};
}

pub use __page_align_down;
