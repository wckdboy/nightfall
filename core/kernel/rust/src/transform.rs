// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use super::{Kernel, KernelError, OperationResult, RigidTransform, Shape};
/// Scalar coordinate locations are zero-based; Axis refers to the complete direction vector.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TransformLocation {
    Translation(usize),
    AxisOrigin(usize),
    AxisDirection(usize),
    Angle,
    Axis,
}
/// Structural transform error. Native normalization/precision-floor checks remain binding.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TransformProblem {
    NonFinite,
    ZeroAxis,
}
impl Kernel {
    /// Check finite fields and an exactly nonzero axis, including when angle is zero.
    /// Native geometry also checks normalization and the configured minimum axis magnitude.
    pub fn validate_transform(&self, input: &RigidTransform) -> Result<(), KernelError> {
        for (values, location) in [
            (
                input.translation,
                TransformLocation::Translation as fn(usize) -> TransformLocation,
            ),
            (input.axis_origin, TransformLocation::AxisOrigin),
            (input.axis_direction, TransformLocation::AxisDirection),
        ] {
            if let Some(axis) = values.iter().position(|v| !v.is_finite()) {
                return Err(invalid(location(axis), TransformProblem::NonFinite));
            }
        }
        if !input.angle.is_finite() {
            return Err(invalid(
                TransformLocation::Angle,
                TransformProblem::NonFinite,
            ));
        }
        if input.axis_direction.iter().all(|v| *v == 0.) {
            return Err(invalid(TransformLocation::Axis, TransformProblem::ZeroAxis));
        }
        Ok(())
    }
    /// Rotate source about the axis/origin, then translate (mm/radians), with full native
    /// validation and history. Inputs stay immutable; retained readers outlive this facade.
    /// Foreign contexts fail before native geometry access. No partial result is returned.
    pub fn transform(
        &self,
        source: &Shape,
        input: &RigidTransform,
    ) -> Result<OperationResult, KernelError> {
        self.validate_transform(input)?;
        Ok(self.native.transform(source, input)?)
    }
}
fn invalid(at: TransformLocation, problem: TransformProblem) -> KernelError {
    KernelError::TransformInput { at, problem }
}
