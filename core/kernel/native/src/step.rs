// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//! Retained immutable AP242 millimetre STEP bytes (RFC 0008).
use crate::{
    Context, NativeError, NonNull, Rc, c_void, extrusion::Shape, status, step_ffi as ffi,
    with_native,
};
/// Native-owned, thread-affine STEP bytes. Clone retains the same immutable allocation.
/// Readers survive all source/result/context drops. Native queries and final release stay
/// serialized on the creating OS thread; ordinary copied bytes may cross threads.
///
/// ```compile_fail,E0277
/// use nightfall_native::step::Step;
/// fn move_step(step: Step) { std::thread::spawn(move || step.bytes().len()); }
/// ```
#[derive(Clone)]
pub struct Step(Rc<Owner>);
struct Owner {
    pointer: NonNull<c_void>,
    bytes: *const u8,
    length: usize,
}
impl Drop for Owner {
    fn drop(&mut self) {
        // SAFETY: final retained allocation; matching release runs serialized on this thread.
        with_native(|| unsafe { ffi::nf_native_step_release(self.pointer.as_ptr()) });
    }
}
impl Step {
    /// Borrow counted bytes, without a trailing NUL contract, for this retained owner's lifetime.
    pub fn bytes(&self) -> &[u8] {
        // SAFETY: checked nonnull immutable native storage with length <= isize::MAX.
        // This Rc retains the independent allocation for the complete borrow.
        unsafe { std::slice::from_raw_parts(self.0.bytes, self.0.length) }
    }
}
fn check_info(info: &ffi::Info) -> Result<usize, NativeError> {
    let length = usize::try_from(info.byte_count).map_err(|_| NativeError::AbiMismatch)?;
    if info.bytes.is_null() || length == 0 || length > isize::MAX as usize {
        return Err(NativeError::AbiMismatch);
    }
    Ok(length)
}
impl Context {
    /// Encode nonempty same-context snapshots, preserving coincident bodies and exact geometry.
    /// Inputs stay immutable; bytes outlive all sources/context. No file I/O or raw pointer escape.
    /// Empty/foreign lists return InvalidArgument before native access; malformed returned
    /// pointer/count metadata returns AbiMismatch. Native geometry failures remain binding.
    pub fn export_step(&self, shapes: &[Shape]) -> Result<Step, NativeError> {
        if shapes.is_empty() {
            return Err(NativeError::InvalidArgument);
        }
        let sources: Vec<_> = shapes
            .iter()
            .map(|shape| shape.native_pointer(self).map(|p| p.as_ptr().cast_const()))
            .collect::<Result<_, _>>()?;
        let count = u64::try_from(sources.len()).map_err(|_| NativeError::InvalidArgument)?;
        let pointer = with_native(|| {
            let mut out = std::ptr::null_mut();
            // SAFETY: retained same-context shapes and a live aligned pointer range/output.
            status(unsafe {
                ffi::nf_native_export_step(self.0.0.as_ptr(), sources.as_ptr(), count, &mut out)
            })?;
            NonNull::new(out).ok_or(NativeError::NativeFailure)
        })?;
        // Adopt outside the guard so all decoding errors release without recursive locking.
        // This byte owner needs no context or geometry owner to destroy its allocation.
        let mut owner = Owner {
            pointer,
            bytes: std::ptr::null(),
            length: 0,
        };
        let info = with_native(|| {
            let mut info = ffi::Info::default();
            // SAFETY: independent live allocation, retained outside this guard, aligned record.
            status(unsafe { ffi::nf_native_step_query(pointer.as_ptr(), &mut info) })?;
            Ok(info)
        })?;
        owner.length = check_info(&info)?;
        owner.bytes = info.bytes;
        Ok(Step(Rc::new(owner)))
    }
}
#[cfg(test)]
#[path = "../tests/support/step_metadata.rs"]
mod tests;
