// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! Safe rust abstraction around a shmem file for use by ashmem.

use kernel::{
    bindings,
    error::{from_err_ptr, to_result, Result},
    fs::file::File,
    miscdevice::{loff_t, IovIter},
    mm::virt::VmArea,
    prelude::*,
    str::CStr,
    types::ARef,
};

use core::{
    cell::UnsafeCell,
    ffi::{c_int, c_ulong},
    ptr::{addr_of_mut, NonNull},
};

/// Wrapper around a file that is known to be a shmem file.
#[derive(Clone)]
pub(crate) struct ShmemFile {
    inner: ARef<File>,
}

impl ShmemFile {
    /// Create a shmem file for use by ashmem.
    ///
    /// This sets up the file with the exact configuration that ashmem needs.
    pub(crate) fn new(name: &CStr, size: usize, flags: usize) -> Result<Self> {
        // SAFETY: The name is a nul-terminated string.
        let vmfile = from_err_ptr(unsafe {
            bindings::shmem_file_setup(name.as_char_ptr(), size as _, flags as c_ulong)
        })?;

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

        set_inode_lockdep_class(&vmfile);

        // SAFETY: We just created the file and have not yet published it, so nobody else is
        // looking at this field yet.
        unsafe { (*vmfile.as_ptr()).f_op = get_shmem_fops((*vmfile.as_ptr()).f_op) };

        Ok(Self { inner: vmfile })
    }

    pub(crate) fn file(&self) -> &File {
        &self.inner
    }

    pub(crate) fn f_pos(&self) -> loff_t {
        // SAFETY: The caller holds the fpos lock on the ashmem file, and we protect the shmem
        // position by the same lock.
        unsafe { self.inner.f_pos() }
    }

    pub(crate) fn set_f_pos(&self, value: loff_t) {
        // SAFETY: The caller holds the fpos lock on the ashmem file, and we protect the shmem
        // position by the same lock.
        unsafe { self.inner.set_f_pos(value) }
    }

    pub(crate) fn vfs_llseek(&self, offset: loff_t, whence: c_int) -> Result<loff_t> {
        // SAFETY: Just an FFI call. The file is valid.
        let ret = unsafe { bindings::vfs_llseek(self.inner.as_ptr(), offset, whence) };

        if ret < 0 {
            Err(Error::from_errno(ret as i32))
        } else {
            Ok(ret)
        }
    }

    pub(crate) fn vfs_iter_read(&self, iov: &mut IovIter, pos: &mut loff_t) -> Result<loff_t> {
        // SAFETY: Just an FFI call. The file and iov is valid.
        let ret = unsafe { bindings::vfs_iter_read(self.inner.as_ptr(), iov.as_raw(), pos, 0) };

        if ret < 0 {
            Err(Error::from_errno(ret as i32))
        } else {
            Ok(ret as loff_t)
        }
    }
}

/// Fix the lockdep class of the shmem inode.
///
/// A separate lockdep class for the backing shmem inodes to resolve the lockdep warning about the
/// race between kswapd taking fs_reclaim before inode_lock and write syscall taking inode_lock and
/// then fs_reclaim. Note that such race is impossible because ashmem does not support write
/// syscalls operating on the backing shmem.
fn set_inode_lockdep_class(vmfile: &File) {
    // SAFETY: This sets the lockdep class correctly.
    unsafe {
        let inode = (*vmfile.as_ptr()).f_inode;
        let lock = addr_of_mut!((*inode).i_rwsem);
        bindings::lockdep_set_class_rwsem(
            lock,
            kernel::static_lock_class!().as_ptr(),
            kernel::c_str!("backing_shmem_inode_class").as_char_ptr(),
        )
    }
}

pub(crate) fn zero_setup(vma: Pin<&mut VmArea>) -> Result<()> {
    // SAFETY: We have exclusive access to this vma.
    to_result(unsafe { bindings::shmem_zero_setup(vma.as_ptr()) })
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
        if (*fops_ptr).mmap.is_none() {
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
