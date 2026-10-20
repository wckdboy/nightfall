// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use crate::{BooleanOp, Kernel, KernelError, OperationResult, Shape};
impl Kernel {
    /// Exact union/subtraction/intersection, with tools interpreted as a union. Duplicate
    /// snapshots retain the native geometry/history deduplication contract. Inputs stay
    /// immutable; complete face/edge history and output readers outlive all input/facade owners.
    ///
    /// Empty/foreign-context operands return Native(InvalidArgument) before native access.
    /// Full native closed-solid validation remains binding: empty/non-solid output returns
    /// Native(NativeFailure), with no partial result. No mesh approximation or file I/O.
    pub fn boolean_operation(
        &self,
        target: &Shape,
        tools: &[Shape],
        operation: BooleanOp,
    ) -> Result<OperationResult, KernelError> {
        Ok(self.native.boolean_operation(target, tools, operation)?)
    }
}
