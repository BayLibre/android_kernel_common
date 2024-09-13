// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

/// Support for declaring a misc device in a static.
#[macro_export]
macro_rules! declare_static_miscdev {
    {
        $(#[$meta:meta])* $pub:vis static $name:ident: $statty:ident;
        registration: $regty:ident;
        device: $t:ty;
        options: $opts:expr;
    } => {
        // Make `$t` accessible from inside `__static_miscdev_internal`.
        type __static_miscdev_internal_ty = $t;

        // Using a module disallows accessing these fields from outside of the macro.
        #[allow(unused_pub)]
        mod __static_miscdev_internal {
            use super::$name;

            type T = super::__static_miscdev_internal_ty;
            type Ptr = <T as $crate::miscdevice::MiscDevice>::Ptr;

            /// Type used by the static misc device.
            ///
            /// # Invariants
            ///
            /// The only value of this type is stored in `$name`.
            #[repr(transparent)]
            pub struct $statty {
                inner: $crate::miscdevice::MiscDeviceRegistration<T>,
            }

            impl $statty {
                /// Register this static misc device.
                ///
                /// # Safety
                ///
                /// This function must not be called more than once.
                pub unsafe fn register(&self) -> $crate::error::Result<$regty> {
                    // SAFETY: The misc device has not yet been registered, and
                    // `MiscDeviceOptions::into_raw` returns a device ready for registration.
                    let res = unsafe { $crate::bindings::misc_register(self.inner.as_raw()) };
                    $crate::error::to_result(res)?;

                    // INVARIANT: We just successfully registered the device. There is not already
                    // a token out there, since `register` is called at most once.
                    Ok($regty(()))
                }

                /// Returns the private data associated with the provided file.
                ///
                /// Returns `None` if the file is not associated with this misc device.
                pub fn try_get_private_data<'a>(
                    &self,
                    file: &'a $crate::fs::LocalFile,
                ) -> Option<<Ptr as $crate::types::ForeignOwnable>::Borrowed<'a>> {
                    // `fops` is always set, so this works even if `inner` is not registered.
                    self.inner.try_get_private_data(file)
                }
            }

            /// Zero-sized token type that represents a registration of the misc device.
            ///
            /// # Invariants
            ///
            /// If a value of this type exists, then `$name` is registered. At most one instance of
            /// this type may exist.
            pub struct $regty(());

            impl $regty {
                /// Destroy the token without deregistering the misc device.
                pub fn forget(self) {
                    ::core::mem::forget(self);
                }
            }

            impl Drop for $regty {
                fn drop(&mut self) {
                    // SAFETY: We know that the device is registered by the type invariants. Since
                    // the destructor is running and there is at most one token, this does not
                    // break the invariants of any other token.
                    unsafe { $crate::bindings::misc_deregister($name.inner.as_raw()) };
                }
            }
        }
        use self::__static_miscdev_internal::{$regty, $statty};

        $(#[$meta])*
        $pub static $name: __static_miscdev_internal::$statty = {
            let opts: $crate::miscdevice::MiscDeviceOptions = $opts;
            // SAFETY: The layouts are compatible.
            // INVARIANT: The value is being stored in `$name`.
            unsafe { ::core::mem::transmute(opts.into_raw::<$t>()) }
        };
    };
}
pub use declare_static_miscdev;
