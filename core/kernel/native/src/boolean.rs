// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//! Exact native solid booleans with immutable sources and retained topology history.
use crate::{
    Context, NativeError, NonNull, boolean_ffi as ffi,
    extrusion::{OperationResult, Shape},
    status, with_native,
};
/// Exact solid operation; tools are treated as a union, with duplicate snapshots deduplicated.
#[repr(u32)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BooleanOp {
    Union,
    Subtract,
    Intersect,
}
impl Context {
    /// Reject empty/foreign operands before native access. Exact native full closed-solid
    /// validation remains binding: empty/non-solid output fails without a partial result.
    /// Inputs stay immutable; result/history readers retain all required snapshots after
    /// every input/context ends. Native calls and releases stay serialized and thread-affine.
    pub fn boolean_operation(
        &self,
        target: &Shape,
        tools: &[Shape],
        operation: BooleanOp,
    ) -> Result<OperationResult, NativeError> {
        if tools.is_empty() {
            return Err(NativeError::InvalidArgument);
        }
        let target = target.native_pointer(self)?;
        let tools: Vec<_> = tools
            .iter()
            .map(|shape| shape.native_pointer(self).map(|p| p.as_ptr().cast_const()))
            .collect::<Result<_, _>>()?;
        let count = u64::try_from(tools.len()).map_err(|_| NativeError::InvalidArgument)?;
        let pointer = with_native(|| {
            let mut out = std::ptr::null_mut();
            // SAFETY: all same-context owners and the aligned pointer range/output remain live;
            // typed mode is valid. The adapter catches exceptions and clears failed outputs.
            status(unsafe {
                ffi::nf_native_boolean(
                    self.0.0.as_ptr(),
                    target.as_ptr(),
                    tools.as_ptr(),
                    count,
                    operation as u32,
                    &mut out,
                )
            })?;
            NonNull::new(out).ok_or(NativeError::NativeFailure)
        })?;
        // Adopt outside the guard so metadata failures cannot recursively lock during cleanup.
        self.adopt_result(pointer)
    }
}
#[cfg(test)]
#[path = "../tests/support/boolean_abi.rs"]
mod tests;
