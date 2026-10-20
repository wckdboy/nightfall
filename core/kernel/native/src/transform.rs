// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//! Exact rigid placement of immutable native solids, with complete retained history.
use super::{
    Context, NativeError, NonNull,
    extrusion::{OperationResult, Shape},
    extrusion_ffi as ffi, status, with_native,
};
/// Rotate about axis/origin, then translate. Millimetres/radians; direction need not be unit.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct RigidTransform {
    pub translation: [f64; 3],
    pub axis_origin: [f64; 3],
    pub axis_direction: [f64; 3],
    pub angle: f64,
}
impl Default for RigidTransform {
    fn default() -> Self {
        Self {
            translation: [0.; 3],
            axis_origin: [0.; 3],
            axis_direction: [0., 0., 1.],
            angle: 0.,
        }
    }
}
impl Context {
    /// Reject foreign contexts before native access; input stays immutable and failures publish nothing.
    /// History owns both snapshots independently of the source/result/context facade's lifetime.
    pub fn transform(
        &self,
        source: &Shape,
        input: &RigidTransform,
    ) -> Result<OperationResult, NativeError> {
        let source = source.native_pointer(self)?;
        let pointer = with_native(|| {
            let mut out = std::ptr::null_mut();
            // SAFETY: immutable source/context/input remain live on their owning thread; output
            // is writable/aligned. The adapter catches exceptions and clears failed outputs.
            status(unsafe {
                ffi::nf_native_transform(self.0.0.as_ptr(), source.as_ptr(), input, &mut out)
            })?;
            NonNull::new(out).ok_or(NativeError::NativeFailure)
        })?;
        // Failed metadata decoding releases only after the native guard has ended.
        self.adopt_result(pointer)
    }
}
