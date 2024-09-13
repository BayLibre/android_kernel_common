// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! Anonymous Shared Memory Subsystem for Android.

use core::{
    ffi::{c_int, c_long},
    pin::Pin,
};
use kernel::{
    bindings, c_str,
    error::Result,
    fs::{File, LocalFile},
    ioctl::_IOC_SIZE,
    miscdevice::{loff_t, IovIter, Kiocb, MiscDevice, MiscDeviceOptions, MiscDeviceRegistration},
    mm::virt::{flags as vma_flags, VmAreaNew},
    page::page_align,
    prelude::*,
    seq_file::{seq_print, SeqFile},
    sync::{new_mutex, Mutex},
    task::Task,
    uaccess::{UserSlice, UserSliceReader, UserSliceWriter},
};

const ASHMEM_NAME_LEN: usize = bindings::ASHMEM_NAME_LEN as usize;
const ASHMEM_FULL_NAME_LEN: usize = bindings::ASHMEM_FULL_NAME_LEN as usize;
const ASHMEM_NAME_PREFIX_LEN: usize = bindings::ASHMEM_NAME_PREFIX_LEN as usize;
const ASHMEM_NAME_PREFIX: [u8; ASHMEM_NAME_PREFIX_LEN] = *b"dev/ashmem/";

const PROT_READ: usize = bindings::PROT_READ as usize;
const PROT_EXEC: usize = bindings::PROT_EXEC as usize;
const PROT_WRITE: usize = bindings::PROT_WRITE as usize;
const PROT_MASK: usize = PROT_EXEC | PROT_READ | PROT_WRITE;

mod shmem;
use shmem::ShmemFile;

fn calc_vm_may_flags(prot: usize) -> usize {
    let mut ret = 0;
    if prot & PROT_READ != 0 {
        ret |= bindings::VM_MAYREAD as usize;
    }
    if prot & PROT_WRITE != 0 {
        ret |= bindings::VM_MAYWRITE as usize;
    }
    if prot & PROT_EXEC != 0 {
        ret |= bindings::VM_MAYEXEC as usize;
    }
    ret
}

/// Convert from `PROT_*` bitmasks to vma flags.
fn calc_vm_prot_bits(prot: usize, pkey: usize) -> usize {
    // Casts are between `usize` and `unsigned long` which are always the same size.
    // SAFETY: This C function is always safe to call.
    unsafe { bindings::calc_vm_prot_bits(prot as _, pkey as _) as usize }
}

/// Does PROT_READ imply PROT_EXEC for this task?
fn read_implies_exec(task: &Task) -> bool {
    // SAFETY: Always safe to read.
    let personality = unsafe { (*task.as_raw()).personality };
    (personality & bindings::READ_IMPLIES_EXEC) != 0
}

module! {
    type: AshmemModule,
    name: "ashmem_rust",
    author: "Alice Ryhl",
    description: "Anonymous Shared Memory Subsystem",
    license: "GPL",
}

struct AshmemModule {
    _misc: Pin<Box<MiscDeviceRegistration<Ashmem>>>,
}

impl kernel::Module for AshmemModule {
    fn init(_module: &'static kernel::ThisModule) -> Result<Self> {
        // SAFETY: Called once since this is the module initializer.
        unsafe { shmem::SHMEM_FOPS_ONCE.init() };

        pr_info!("Using Rust implementation.");

        Ok(Self {
            _misc: Box::pin_init(
                MiscDeviceRegistration::register(MiscDeviceOptions {
                    name: c_str!("ashmem"),
                }),
                GFP_KERNEL,
            )?,
        })
    }
}

/// Represents an open ashmem file.
#[pin_data]
struct Ashmem {
    #[pin]
    inner: Mutex<AshmemInner>,
}

struct AshmemInner {
    size: usize,
    prot_mask: usize,
    /// If set, then this holds the ashmem name without the dev/ashmem/ prefix. No zero terminator.
    name: Option<Vec<u8>>,
    file: Option<ShmemFile>,
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
                        prot_mask: PROT_MASK,
                        name: None,
                        file: None,
                    }),
                }
            },
            GFP_KERNEL,
        )
    }

    fn mmap(me: Pin<&Ashmem>, _file: &File, vma: &VmAreaNew) -> Result<()> {
        let asma = &mut *me.inner.lock();

        // User needs to SET_SIZE before mapping.
        if asma.size == 0 {
            return Err(EINVAL);
        }

        // Requested mapping size larger than object size.
        if vma.end() - vma.start() > page_align(asma.size) {
            return Err(EINVAL);
        }

        let vma_flags = vma.flags();
        let allowed_prot_bits = calc_vm_prot_bits(asma.prot_mask, 0);
        let extra_vma_flags = vma_flags & !allowed_prot_bits;
        if extra_vma_flags & calc_vm_prot_bits(PROT_MASK, 0) != 0 {
            return Err(EPERM);
        }

        let flags_clear = calc_vm_may_flags(!asma.prot_mask);
        vma.set_flags(vma_flags & !flags_clear);

        let file = match asma.file.as_ref() {
            Some(file) => file,
            None => {
                let mut full_name = [0u8; ASHMEM_FULL_NAME_LEN];
                let name_len = asma.full_name(&mut full_name);
                let name = CStr::from_bytes_with_nul(&full_name[..name_len])?;
                asma.file
                    .insert(ShmemFile::new(name, asma.size, vma.flags())?)
            }
        };

        if vma.flags() & vma_flags::SHARED != 0 {
            // We're really using this just to set vm_ops to `shmem_anon_vm_ops`. Anything else it
            // does is undone by the call to `set_file` below.
            shmem::zero_setup(vma)?;
        } else {
            vma.set_anonymous();
        }

        vma.set_file(file.file());
        Ok(())
    }

    fn llseek(me: Pin<&Ashmem>, file: &LocalFile, offset: loff_t, whence: c_int) -> Result<loff_t> {
        let asma_file = {
            let asma = me.inner.lock();
            if asma.size == 0 {
                return Err(EINVAL);
            }
            match asma.file.as_ref() {
                Some(asma_file) => asma_file.clone(),
                None => return Err(EBADF),
            }
        };

        let ret = asma_file.vfs_llseek(offset, whence)?;

        // SAFETY: We protect the shmem file with the same mechanism as the ashmem file. We are in
        // llseek, so our caller ensures that accessing f_pos is okay.
        unsafe { shmem::file_set_fpos(file, shmem::file_get_fpos(asma_file.file())) };

        Ok(ret)
    }

    fn read_iter(mut kiocb: Kiocb<'_, Self::Ptr>, iov: &mut IovIter) -> Result<usize> {
        let me = kiocb.private_data();
        let asma_file = {
            let asma = me.inner.lock();
            if asma.size == 0 {
                // If size is not set, or set to 0, always return EOF.
                return Ok(0);
            }
            match asma.file.as_ref() {
                Some(asma_file) => asma_file.clone(),
                None => return Err(EBADF),
            }
        };

        let ret = asma_file.vfs_iter_read(iov, kiocb.ki_pos_mut())?;

        // SAFETY: We protect the shmem file with the same mechanism as the ashmem file. We are in
        // read_iter, so our caller ensures that accessing f_pos is okay.
        unsafe { shmem::file_set_fpos(asma_file.file(), kiocb.ki_pos()) };

        Ok(ret as usize)
    }

    fn ioctl(me: Pin<&Ashmem>, _file: &File, cmd: u32, arg: usize) -> Result<c_long> {
        let size = _IOC_SIZE(cmd);
        match cmd {
            bindings::ASHMEM_SET_NAME => me.set_name(UserSlice::new(arg, size).reader()),
            bindings::ASHMEM_GET_NAME => me.get_name(UserSlice::new(arg, size).writer()),
            bindings::ASHMEM_SET_SIZE => me.set_size(arg),
            bindings::ASHMEM_GET_SIZE => me.get_size(),
            bindings::ASHMEM_SET_PROT_MASK => me.set_prot_mask(arg),
            bindings::ASHMEM_GET_PROT_MASK => me.get_prot_mask(),
            bindings::ASHMEM_GET_FILE_ID => me.get_file_id(UserSlice::new(arg, size).writer()),
            _ => Err(EINVAL),
        }
    }

    #[cfg(CONFIG_COMPAT)]
    fn compat_ioctl(me: Pin<&Ashmem>, file: &File, compat_cmd: u32, arg: usize) -> Result<c_long> {
        let cmd = match compat_cmd {
            bindings::COMPAT_ASHMEM_SET_SIZE => bindings::ASHMEM_SET_SIZE,
            bindings::COMPAT_ASHMEM_SET_PROT_MASK => bindings::ASHMEM_SET_PROT_MASK,
            _ => compat_cmd,
        };
        Self::ioctl(me, file, cmd, arg)
    }

    #[cfg(CONFIG_PROC_FS)]
    fn show_fdinfo(me: Pin<&Ashmem>, _file: &File, m: &SeqFile) {
        let asma = me.inner.lock();

        if let Some(file) = asma.file.as_ref() {
            seq_print!(m, "inode:\t{}\n", file.inode_ino());
        }
        if let Some(name) = asma.name.as_ref() {
            let name = core::str::from_utf8(name).unwrap_or("<invalid utf-8>");
            seq_print!(m, "name:\t{}\n", name);
        }
        seq_print!(m, "size\t{}\n", asma.size);
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
        if asma.file.is_some() {
            return Err(EINVAL);
        }
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
        if asma.file.is_some() {
            return Err(EINVAL);
        }
        asma.size = size;
        Ok(0)
    }

    fn get_size(&self) -> Result<c_long> {
        Ok(self.inner.lock().size as c_long)
    }

    fn set_prot_mask(&self, mut prot: usize) -> Result<c_long> {
        let mut asma = self.inner.lock();

        // The user can only remove, not add, protection bits.
        if (asma.prot_mask & prot) != prot {
            return Err(EINVAL);
        }

        if (prot & PROT_READ != 0) && read_implies_exec(current!()) {
            prot |= PROT_EXEC;
        }

        asma.prot_mask = prot;
        Ok(0)
    }

    fn get_prot_mask(&self) -> Result<c_long> {
        Ok(self.inner.lock().prot_mask as c_long)
    }

    fn get_file_id(&self, mut writer: UserSliceWriter) -> Result<c_long> {
        let ino = {
            let asma = self.inner.lock();
            let Some(file) = asma.file.as_ref() else {
                return Err(EINVAL);
            };
            file.inode_ino()
        };
        writer.write(&ino)?;
        Ok(0)
    }
}

impl AshmemInner {
    /// Get the full name. Returns the length, including the nul terminator.
    ///
    /// If the name is `Some(name)`, then this returns `dev/ashmem/name\0`.
    ///
    /// If the name is `None`, then this returns `dev/ashmem\0`.
    fn full_name(&self, name: &mut [u8; ASHMEM_FULL_NAME_LEN]) -> usize {
        name[..ASHMEM_NAME_PREFIX_LEN].copy_from_slice(&ASHMEM_NAME_PREFIX);
        if let Some(set_name) = self.name.as_deref() {
            name[ASHMEM_NAME_PREFIX_LEN..][..set_name.len()].copy_from_slice(set_name);
        } else {
            // Remove last slash if no name set.
            name[ASHMEM_NAME_PREFIX_LEN - 1] = 0;
        }
        name[ASHMEM_FULL_NAME_LEN - 1] = 0;

        // This unwrap only fails if there's no nul-byte, but we just added one at the end above.
        name.iter()
            .position(|&c| c == 0)
            .map(|len| len + 1)
            .unwrap()
    }
}
