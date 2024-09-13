// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! Anonymous Shared Memory Subsystem for Android.

use core::{ffi::c_long, pin::Pin};
use kernel::{
    bindings::{self, ASHMEM_NAME_LEN},
    c_str,
    error::Result,
    fs::File,
    ioctl::_IOC_SIZE,
    miscdevice::{declare_static_miscdev, MiscDevice, MiscDeviceOptions},
    prelude::*,
    sync::{new_mutex, Mutex},
    uaccess::{UserSlice, UserSliceReader, UserSliceWriter},
};

module! {
    type: AshmemModule,
    name: "ashmem_rust",
    author: "Alice Ryhl",
    description: "Anonymous Shared Memory Subsystem",
    license: "GPL",
}

struct AshmemModule {
    _misc: AshmemMiscdevRegistration,
}

impl kernel::Module for AshmemModule {
    fn init(_module: &'static kernel::ThisModule) -> Result<Self> {
        pr_warn!("Ashmem Rust initialized.");

        Ok(Self {
            // SAFETY: There is no previous call to `register` on `ASHMEM_MISCDEV` since this is
            // the module initializer.
            _misc: unsafe { ASHMEM_MISCDEV.register() }?,
        })
    }
}

declare_static_miscdev! {
    static ASHMEM_MISCDEV: AshmemMiscdev;
    registration: AshmemMiscdevRegistration;
    device: Ashmem;
    options: MiscDeviceOptions {
        name: c_str!("ashmem_rust")
    };
}

/// Represents an open ashmem file.
#[pin_data]
struct Ashmem {
    #[pin]
    inner: Mutex<AshmemInner>,
}

struct AshmemInner {
    size: usize,
    /// If set, then this holds the ashmem name without the dev/ashmem/ prefix. No zero terminator.
    name: Option<Vec<u8>>,
}

#[vtable]
impl MiscDevice for Ashmem {
    type Ptr = Pin<Box<Self>>;

    fn open(_: &File) -> Result<Pin<Box<Self>>> {
        Box::try_pin_init(
            try_pin_init! {
                Ashmem {
                    inner <- new_mutex!(AshmemInner {
                        size: 0,
                        name: None,
                    }),
                }
            },
            GFP_KERNEL,
        )
    }

    fn ioctl(me: Pin<&Ashmem>, _file: &File, cmd: u32, arg: usize) -> Result<c_long> {
        let size = _IOC_SIZE(cmd);
        match cmd {
            bindings::ASHMEM_SET_NAME => me.set_name(UserSlice::new(arg, size).reader()),
            bindings::ASHMEM_GET_NAME => me.get_name(UserSlice::new(arg, size).writer()),
            bindings::ASHMEM_SET_SIZE => me.set_size(arg),
            bindings::ASHMEM_GET_SIZE => me.get_size(),
            _ => Err(EINVAL),
        }
    }
}

impl Ashmem {
    fn set_name(&self, mut reader: UserSliceReader) -> Result<c_long> {
        let mut local_name = [0u8; ASHMEM_NAME_LEN];
        reader.read_slice(&mut local_name)?;
        let zero_pos = local_name.iter().position(|&c| c == 0).ok_or(EINVAL)?;
        let mut v = Vec::with_capacity(zero_pos, GFP_KERNEL)?;
        v.extend_from_slice(&local_name[..zero_pos], GFP_KERNEL)?;

        let mut asma = self.inner.lock();
        asma.name = Some(v);
        Ok(0)
    }

    fn get_name(&self, mut writer: UserSliceWriter) -> Result<c_long> {
        let mut local_name = [0u8; ASHMEM_NAME_LEN];
        let asma = self.inner.lock();
        let name = asma.name.as_deref().unwrap_or(b"dev/ashmem");
        let len = name.len();
        local_name[..len].copy_from_slice(name);
        drop(asma);

        let let_with_nul = len + 1;
        local_name[len] = 0;

        writer.write_slice(&local_name[..let_with_nul])?;
        Ok(0)
    }

    fn set_size(&self, size: usize) -> Result<c_long> {
        let mut asma = self.inner.lock();
        asma.size = size;
        Ok(0)
    }

    fn get_size(&self) -> Result<c_long> {
        Ok(self.inner.lock().size as c_long)
    }
}
