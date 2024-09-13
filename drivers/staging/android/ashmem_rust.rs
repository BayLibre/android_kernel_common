// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! Anonymous Shared Memory Subsystem for Android.

use core::pin::Pin;
use kernel::{
    c_str,
    error::Result,
    fs::File,
    miscdevice::{declare_static_miscdev, MiscDevice, MiscDeviceOptions},
    prelude::*,
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
struct Ashmem {}

#[vtable]
impl MiscDevice for Ashmem {
    type Ptr = Pin<Box<Self>>;

    fn open(_: &File) -> Result<Pin<Box<Self>>> {
        Box::try_pin_init(Ashmem {}, GFP_KERNEL)
    }
}
