// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! Provides knobs for Rust ashmem.

use crate::ashmem_range;
use core::marker::PhantomData;
use kernel::{
    c_str,
    fs::File,
    miscdevice::{IovIter, Kiocb, MiscDevice, MiscDeviceOptions, MiscDeviceRegistration},
    prelude::*,
};

pub(crate) trait AshmemToggle {
    const NAME: &'static CStr;
    fn set(enabled: bool) -> Result<()>;
    fn get() -> bool;
}

pub(crate) struct AshmemToggleMisc<T>(PhantomData<T>);

impl<T: AshmemToggle> AshmemToggleMisc<T> {
    pub(crate) fn new() -> Result<Pin<Box<MiscDeviceRegistration<AshmemToggleMisc<T>>>>> {
        Box::pin_init(
            MiscDeviceRegistration::register(MiscDeviceOptions { name: T::NAME }),
            GFP_KERNEL,
        )
    }
}

#[vtable]
impl<T: AshmemToggle> MiscDevice for AshmemToggleMisc<T> {
    type Ptr = ();
    fn open(_: &File, _: &MiscDeviceRegistration<Self>) -> Result<()> {
        Ok(())
    }
    fn read_iter(mut kiocb: Kiocb<'_, Self::Ptr>, iov: &mut IovIter) -> Result<usize> {
        if kiocb.ki_pos() != 0 {
            return Ok(0);
        }

        let data = match T::get() {
            false => b"0\n",
            true => b"1\n",
        };

        // You better give me a buffer with space for at least two bytes.
        iov.copy_to_iter(data)?;
        *kiocb.ki_pos_mut() = 2;
        Ok(2)
    }
    fn write_iter(_kiocb: Kiocb<'_, Self::Ptr>, iov: &mut IovIter) -> Result<usize> {
        let enable = match iov.copy_from_iter::<u8>()? {
            b'0' => false,
            b'1' => true,
            b if b.is_ascii_whitespace() => return Ok(1),
            _ => return Err(EINVAL),
        };
        T::set(enable)?;
        Ok(1)
    }
}

pub(crate) struct AshmemToggleShrinker;

impl AshmemToggle for AshmemToggleShrinker {
    const NAME: &'static CStr = c_str!("ashmem_unpinning_enable");
    fn set(enabled: bool) -> Result<()> {
        ashmem_range::set_shrinker_enabled(enabled)
    }
    fn get() -> bool {
        ashmem_range::get_shrinker_enabled()
    }
}
