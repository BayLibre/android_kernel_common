// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! Miscdevice support.
//!
//! C headers: [`include/linux/miscdevice.h`](srctree/include/linux/miscdevice.h).
//!
//! Reference: <https://www.kernel.org/doc/html/latest/driver-api/misc_devices.html>

use crate::{
    bindings,
    error::{to_result, Error, Result, VTABLE_DEFAULT_ERROR},
    fs::{File, LocalFile},
    prelude::*,
    str::CStr,
    types::{ForeignOwnable, Opaque},
};
use core::{
    ffi::{c_int, c_long, c_uint, c_ulong},
    marker::PhantomData,
    mem::MaybeUninit,
    pin::Pin,
};

mod static_reg;
pub use self::static_reg::declare_static_miscdev;

/// Options for creating a misc device.
#[derive(Copy, Clone)]
pub struct MiscDeviceOptions {
    /// The name of the miscdevice.
    pub name: &'static CStr,
}

impl MiscDeviceOptions {
    /// Create a raw miscdev ready for registration.
    pub const fn into_raw<T: MiscDevice>(self) -> bindings::miscdevice {
        // SAFETY: All zeros is valid for this C type.
        let mut result: bindings::miscdevice = unsafe { MaybeUninit::zeroed().assume_init() };
        result.minor = bindings::MISC_DYNAMIC_MINOR as _;
        result.name = self.name.as_char_ptr();
        result.fops = create_vtable::<T>();
        result
    }
}

/// A registration of a miscdevice.
///
/// # Invariants
///
/// `inner` is a registered misc device.
#[repr(transparent)]
#[pin_data(PinnedDrop)]
pub struct MiscDeviceRegistration<T> {
    #[pin]
    inner: Opaque<bindings::miscdevice>,
    _t: PhantomData<T>,
}

unsafe impl<T> Send for MiscDeviceRegistration<T> {}
unsafe impl<T> Sync for MiscDeviceRegistration<T> {}

impl<T: MiscDevice> MiscDeviceRegistration<T> {
    /// Register a misc device.
    pub fn register(opts: MiscDeviceOptions) -> impl PinInit<Self, Error> {
        try_pin_init!(Self {
            inner <- Opaque::try_ffi_init(move |slot: *mut bindings::miscdevice| {
                // SAFETY: The initializer can write to the provided `slot`.
                unsafe { slot.write(opts.into_raw::<T>()) };

                // SAFETY: We just wrote the misc device options to the slot. The miscdevice will
                // get unregistered before `slot` is deallocated because the memory is pinned and
                // the destructor of this type deallocates the memory.
                // INVARIANT: If this returns `Ok(())`, then the `slot` will contain a registered
                // misc device.
                to_result(unsafe { bindings::misc_register(slot) })
            }),
            _t: PhantomData,
        })
    }

    /// Returns the private data associated with the provided file.
    ///
    /// Returns `None` if the file is not associated with this misc device.
    pub fn try_get_private_data<'a>(
        &self,
        file: &'a LocalFile,
    ) -> Option<<T::Ptr as ForeignOwnable>::Borrowed<'a>> {
        // SAFETY: `fops` of a miscdevice is immutable after initialization.
        let fops_this = unsafe { (*self.as_raw()).fops };
        // SAFETY: `f_op` of a file is immutable after initialization.
        let fops_file = unsafe { (*file.as_ptr()).f_op };

        if core::ptr::eq(fops_this, fops_file) {
            // SAFETY: We know that `file` is associated with a `MiscDeviceRegistration<T>`, so
            // `private_data` is immutable.
            let private_data = unsafe { (*file.as_ptr()).private_data };
            // SAFETY:
            // * The fops match, so the file's private date has the right type.
            // * The returned borrow cannot outlive the file.
            Some(unsafe { <T::Ptr as ForeignOwnable>::borrow(private_data) })
        } else {
            None
        }
    }

    /// Returns a raw pointer to the misc device.
    pub fn as_raw(&self) -> *mut bindings::miscdevice {
        self.inner.get()
    }
}

#[pinned_drop]
impl<T> PinnedDrop for MiscDeviceRegistration<T> {
    fn drop(self: Pin<&mut Self>) {
        // SAFETY: We know that the device is registered by the type invariants.
        unsafe { bindings::misc_deregister(self.inner.get()) };
    }
}

/// Trait implemented by the private data of an open misc device.
#[vtable]
pub trait MiscDevice {
    /// What kind of pointer should `Self` be wrapped in.
    type Ptr: ForeignOwnable + Send + Sync;

    /// Called when the misc device is opened.
    ///
    /// The returned pointer will be stored as the private data for the file.
    fn open(_file: &File) -> Result<Self::Ptr>;

    /// Called when the misc device is released.
    fn release(device: Self::Ptr, _file: &File) {
        drop(device);
    }

    /// Handler for ioctls
    ///
    /// The `cmd` argument is usually manipulated using the utilties in [`kernel::ioctl`].
    ///
    /// [`kernel::ioctl`]: mod@crate::ioctl
    fn ioctl(
        _device: <Self::Ptr as ForeignOwnable>::Borrowed<'_>,
        _file: &File,
        _cmd: u32,
        _arg: usize,
    ) -> Result<()> {
        kernel::build_error(VTABLE_DEFAULT_ERROR)
    }

    #[cfg(FOO)]
    fn compat_ioctl(
        device: <Self::Ptr as ForeignOwnable>::Borrowed<'_>,
        file: &File,
        cmd: u32,
        arg: usize,
    ) -> Result<()> {
        Self::ioctl(device, file, cmd, arg)
    }
}

const fn create_vtable<T: MiscDevice>() -> &'static bindings::file_operations {
    struct VtableHelper<T: MiscDevice> {
        _t: PhantomData<T>,
    }
    impl<T: MiscDevice> VtableHelper<T> {
        const VTABLE: bindings::file_operations = bindings::file_operations {
            open: Some(fops_open::<T>),
            release: Some(fops_release::<T>),
            unlocked_ioctl: if T::HAS_IOCTL {
                Some(fops_ioctl::<T>)
            } else {
                None
            },
            ..unsafe { MaybeUninit::zeroed().assume_init() }
        };
    }

    &VtableHelper::<T>::VTABLE
}

unsafe extern "C" fn fops_open<T: MiscDevice>(
    inode: *mut bindings::inode,
    file: *mut bindings::file,
) -> c_int {
    // SAFETY: The pointers are valid and for a file being opened.
    let ret = unsafe { bindings::generic_file_open(inode, file) };
    if ret != 0 {
        return ret;
    }

    // SAFETY:
    // * The file is valid for the duration of this call.
    // * There is no active fdget_pos region on the file on this thread.
    let ptr = match T::open(unsafe { File::from_raw_file(file) }) {
        Ok(ptr) => ptr,
        Err(err) => return err.to_errno(),
    };

    // SAFETY: The open call of a file owns the private data.
    unsafe { (*file).private_data = ptr.into_foreign().cast_mut() };

    0
}

unsafe extern "C" fn fops_release<T: MiscDevice>(
    _inode: *mut bindings::inode,
    file: *mut bindings::file,
) -> c_int {
    // SAFETY: The release call of a file owns the private data.
    let private = unsafe { (*file).private_data };
    // SAFETY: We are taking ownership of the private data, so we can drop it.
    let ptr = unsafe { <T::Ptr as ForeignOwnable>::from_foreign(private) };
    // SAFETY:
    // * The file is valid for the duration of this call.
    // * There is no active fdget_pos region on the file on this thread.
    T::release(ptr, unsafe { File::from_raw_file(file) });

    0
}

unsafe extern "C" fn fops_ioctl<T: MiscDevice>(
    file: *mut bindings::file,
    cmd: c_uint,
    arg: c_ulong,
) -> c_long {
    // SAFETY: The release call of a file owns the private data.
    let private = unsafe { (*file).private_data };
    // SAFETY: Ioctl calls can borrow the private data of the file.
    let device = unsafe { <T::Ptr as ForeignOwnable>::borrow(private) };
    // SAFETY:
    // * The file is valid for the duration of this call.
    // * There is no active fdget_pos region on the file on this thread.
    let file = unsafe { File::from_raw_file(file) };

    match T::ioctl(device, file, cmd as u32, arg as usize) {
        Ok(()) => 0,
        Err(err) => err.to_errno() as c_long,
    }
}
