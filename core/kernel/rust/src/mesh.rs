// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use crate::{Kernel, KernelError, Mesh, MeshingOptions, Shape};
/// Position in mesh options, with mm/radian units and a count limit.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MeshLocation {
    LinearDeflection,
    AngularDeflection,
    TriangleLimit,
}
/// Rust policy failure; solver-specific meshing errors remain native errors.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MeshProblem {
    NonFinite,
    BelowTolerance,
    OutOfRange,
}
impl Kernel {
    /// Check finite deflections against configured precision floors, angular <= π and
    /// 1..=1M triangles. Native validation and its 3M-vertex limit remain binding.
    pub fn validate_meshing(&self, options: &MeshingOptions) -> Result<(), KernelError> {
        let invalid = |at, problem| KernelError::MeshInput { at, problem };
        for (at, value, floor) in [
            (
                MeshLocation::LinearDeflection,
                options.linear_deflection,
                self.linear_mm,
            ),
            (
                MeshLocation::AngularDeflection,
                options.angular_deflection,
                self.angular_radians,
            ),
        ] {
            if !value.is_finite() {
                return Err(invalid(at, MeshProblem::NonFinite));
            }
            if value < floor {
                return Err(invalid(at, MeshProblem::BelowTolerance));
            }
        }
        if options.angular_deflection > std::f64::consts::PI {
            return Err(invalid(
                MeshLocation::AngularDeflection,
                MeshProblem::OutOfRange,
            ));
        }
        if options.max_triangles == 0 || options.max_triangles > 1_000_000 {
            return Err(invalid(
                MeshLocation::TriangleLimit,
                MeshProblem::OutOfRange,
            ));
        }
        Ok(())
    }
    /// Mesh an immutable copy, retaining exact-source face associations and native buffers.
    /// Readers survive facade/result teardown; transient triangle offsets are never selectors.
    pub fn tessellate(&self, shape: &Shape, options: &MeshingOptions) -> Result<Mesh, KernelError> {
        self.validate_meshing(options)?;
        Ok(self.native.tessellate(shape, options)?)
    }
}
