// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! Safe rust abstraction around a shmem file for use by ashmem.
#![allow(dead_code)]

use kernel::{
    bindings,
    error::{from_err_ptr, Result},
    types::ARef,
    fs::file::File,
    prelude::*,
    str::CStr,
};

use core::{cell::UnsafeCell, ptr::NonNull, ffi::{c_ulong, c_int}};

/// Wrapper around a file that is known to be a shmem file.
pub(crate) struct ShmemFile {
    inner: ARef<File>,
}

impl ShmemFile {
    /// Create a shmem file for use by ashmem.
    ///
    /// This sets up the file with the exact configuration that ashmem needs.
    pub(crate) fn new(name: &CStr, size: usize, flags: u32) -> Result<Self> {
        // SAFETY: The name is a nul-terminated string.
        let vmfile =
            from_err_ptr(unsafe { bindings::shmem_file_setup(name.as_char_ptr(), size as _, flags as c_ulong) })?;

        // SAFETY: The call to `shmem_file_setup` was successful, so `vmfile` is a valid pointer to
        // a file and we can transfer ownership of the refcount it created to an `ARef<File>`.
        let vmfile = unsafe { ARef::<File>::from_raw(NonNull::new_unchecked(vmfile.cast())) };

        // The `kernel::fs` abstraction does not provide a function to set the mode, so do it
        // directly.
        //
        // TODO: Do we really need to set this? Shmem files implement seeking, so why wouldn't they
        // be available for seeking?
        //
        // SAFETY: We just created the file and have not yet published it, so nobody else is
        // looking at this field yet.
        unsafe { (*vmfile.as_ptr()).f_mode |= bindings::FMODE_LSEEK };

        /*
        // SAFETY: Reading the inode of a file is always okay.
        let inode = unsafe { vmfile.as_ptr().f_inode };
        unsafe {
            bindings::lockdep_set_class(addr_of!((*inode).i_rwsem), static_lock_class!().as_ptr())
        };
        */

        // SAFETY: We just created the file and have not yet published it, so nobody else is
        // looking at this field yet.
        unsafe { (*vmfile.as_ptr()).f_op = get_shmem_fops((*vmfile.as_ptr()).f_op) };

        Ok(Self { inner: vmfile })
    }
}

/// # Safety
///
/// Must only be used with the fops of a shmem file.
unsafe fn get_shmem_fops(
    shmem_fops: *const bindings::file_operations,
) -> &'static bindings::file_operations {
    struct FopsHelper {
        inner: UnsafeCell<bindings::file_operations>,
    }
    unsafe impl Sync for FopsHelper {}

    static VMFILE_FOPS: FopsHelper = FopsHelper {
        // SAFETY: All zeros is valid for `struct file_operations`.
        inner: UnsafeCell::new(unsafe { core::mem::zeroed() }),
    };

    let fops_ptr = VMFILE_FOPS.inner.get();

    // SAFETY: We know that `shmem_fops` is a valid shmem file operations, so we just copy it over
    // to `VMFILE_FOPS`. This could technically cause a data race if initialized by two threads in
    // parallel. This is not really okay. For example, if another thread is initializing it, we may
    // see `mmap` having been written even if we don't yet see the writes to other parts of the
    // fops. This means that reading from fops after skipping this `if` technically doesn't
    // guarantee that all of the function pointers are set yet.
    //
    // TODO: Fix this to avoid the above data race problem.
    unsafe {
        if (*fops_ptr).mmap.is_some() {
            let mut new_fops = *shmem_fops;
            new_fops.mmap = Some(ashmem_vmfile_mmap);
            new_fops.get_unmapped_area = Some(ashmem_vmfile_get_unmapped_area);
            *fops_ptr = new_fops;
        }
    }

    // SAFETY: We initialized `VMFILE_FOPS`, so it's not going to change anymore.
    unsafe { &*fops_ptr }
}

extern "C" fn ashmem_vmfile_mmap(
    _file: *mut bindings::file,
    _vma: *mut bindings::vm_area_struct,
) -> c_int {
    EPERM.to_errno()
}

unsafe extern "C" fn ashmem_vmfile_get_unmapped_area(
    file: *mut bindings::file,
    addr: c_ulong,
    len: c_ulong,
    pgoff: c_ulong,
    flags: c_ulong,
) -> c_ulong {
    let mm = unsafe { (*bindings::get_current()).mm };
    unsafe { bindings::mm_get_unmapped_area(mm, file, addr, len, pgoff, flags) }
}
