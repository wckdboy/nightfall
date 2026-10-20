// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//! Checked service-private ownership of the native geometry backend (RFC 0008).
#![deny(unsafe_op_in_unsafe_fn)]
use std::ffi::c_void;
use std::ptr::NonNull;
use std::rc::Rc;
use std::sync::Mutex;
pub mod extrusion;
mod extrusion_ffi;
pub mod mesh;
mod mesh_ffi;
pub mod transform;
const ABI: u32 = 1;
static NATIVE_CALL: Mutex<()> = Mutex::new(());
fn with_native<T>(operation: impl FnOnce() -> T) -> T {
    // Only serialization lives in this mutex; recover poison so final release never panics.
    let _guard = NATIVE_CALL
        .lock()
        .unwrap_or_else(std::sync::PoisonError::into_inner);
    operation()
}
#[repr(C)]
#[derive(Default)]
struct RawInfo {
    abi_version: u32,
    capabilities: u32,
    identity: *const u8,
    identity_bytes: usize,
}
unsafe extern "C" {
    fn nf_native_context_create(abi: u32, linear: f64, angular: f64, out: *mut *mut c_void) -> u32;
    fn nf_native_context_info(context: *const c_void, out: *mut RawInfo) -> u32;
    fn nf_native_context_release(context: *mut c_void);
}
fn status(value: u32) -> Result<(), NativeError> {
    match value {
        0 => Ok(()),
        1 => Err(NativeError::InvalidArgument),
        2 => Err(NativeError::Unavailable),
        3 => Err(NativeError::NativeFailure),
        _ => Err(NativeError::AbiMismatch),
    }
}
/// Complete native failures; no partially initialized context is returned.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum NativeError {
    InvalidArgument,
    Unavailable,
    NativeFailure,
    AbiMismatch,
}
#[derive(Clone, Copy, Debug)]
#[repr(u8)]
/// Native solver operations; availability does not claim Rust operation parity.
pub enum Operation {
    Extrude,
    RigidTransform,
    Booleans,
    Revolve,
    Mirror,
    Chamfer,
    Fillet,
    Loft,
    Sweep,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
/// Immutable capability set copied from the selected native backend.
pub struct Capabilities(u32);
impl Capabilities {
    /// Whether the native solver advertises this operation.
    pub fn supports(self, operation: Operation) -> bool {
        self.0 & (1 << operation as u8) != 0
    }
}
#[derive(Debug)]
/// Owned metadata, independent of the context's lifetime; not a persisted cache key yet.
pub struct BackendInfo {
    pub identity: String,
    pub capabilities: Capabilities,
}
struct Owner(NonNull<c_void>);
impl Drop for Owner {
    fn drop(&mut self) {
        // SAFETY: the final Rc owns this live pointer; creation and release use one module.
        with_native(|| unsafe { nf_native_context_release(self.0.as_ptr()) });
    }
}
/// Thread-affine, shared ownership. Clone retains the backend; final drop releases it.
/// Neither raw pointers nor native lifetimes escape this checked boundary.
#[doc = include_str!("../tests/thread_affinity.md")]
#[derive(Clone)]
pub struct Context(Rc<Owner>);
impl Context {
    /// Create a native context using finite positive mm/radian tolerances (OCCT floors apply).
    /// An unconfigured backend reports Unavailable. Calls and destruction are serialized.
    pub fn new(linear_mm: f64, angular_radians: f64) -> Result<Self, NativeError> {
        let pointer = with_native(|| {
            let mut raw = std::ptr::null_mut();
            // SAFETY: writable aligned output; adapter catches exceptions and clears on error.
            status(unsafe { nf_native_context_create(ABI, linear_mm, angular_radians, &mut raw) })?;
            NonNull::new(raw).ok_or(NativeError::NativeFailure)
        })?;
        Ok(Self(Rc::new(Owner(pointer))))
    }
    /// Copy immutable UTF-8 identity/capabilities while retaining and serializing the owner.
    pub fn info(&self) -> Result<BackendInfo, NativeError> {
        with_native(|| {
            let mut raw = RawInfo::default();
            // SAFETY: Rc retains a live same-thread context; output is writable and aligned.
            status(unsafe { nf_native_context_info(self.0.0.as_ptr(), &mut raw) })?;
            if raw.abi_version != ABI || raw.capabilities & !511 != 0 {
                return Err(NativeError::AbiMismatch);
            }
            if raw.identity.is_null()
                || raw.identity_bytes == 0
                || raw.identity_bytes > isize::MAX as usize
            {
                return Err(NativeError::NativeFailure);
            }
            // SAFETY: adapter guarantees this immutable byte range until owner release. Rc and
            // guard retain it for the complete copy; the count fits Rust's slice size limit.
            let bytes = unsafe { std::slice::from_raw_parts(raw.identity, raw.identity_bytes) };
            let identity = std::str::from_utf8(bytes)
                .map_err(|_| NativeError::NativeFailure)?
                .to_owned();
            Ok(BackendInfo {
                identity,
                capabilities: Capabilities(raw.capabilities),
            })
        })
    }
}
#[cfg(test)]
#[path = "../tests/support/extrusion_abi.rs"]
mod extrusion_abi_tests;
#[cfg(test)]
#[path = "../tests/support/internal.rs"]
mod tests;
