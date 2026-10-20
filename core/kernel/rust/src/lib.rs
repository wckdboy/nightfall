// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//! Headless typed kernel policy (RFC 0008); exact geometry remains native C++/OCCT.
#![forbid(unsafe_code)]
pub use nightfall_math::{KnotVector, MathError, NurbsCurve2};
use nightfall_native::Context;
pub use nightfall_native::extrusion::{
    Entity, EntityKind, Extrusion, Frame, Generated, History, Measures, OperationResult,
    ProfileEdge, Region, Shape,
};
pub use nightfall_native::{BackendInfo, NativeError, Operation as NativeOperation};
/// Position in an extrusion input. Loop zero is outer; holes start at one.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum InputLocation {
    FrameOrigin(usize),
    FrameXAxis(usize),
    FrameNormal(usize),
    Displacement(usize),
    Regions,
    Loop {
        region: usize,
        loop_index: usize,
    },
    Curve {
        region: usize,
        loop_index: usize,
        curve: usize,
    },
}
/// Structural input failure; disconnected means the curve's end misses the next start.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum InputProblem {
    NonFinite,
    Empty,
    Disconnected,
    Evaluation(MathError),
}
/// A complete failure, with source location for Rust input checks; never a partial solid.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum KernelError {
    Input {
        at: InputLocation,
        problem: InputProblem,
    },
    Native(NativeError),
}
impl From<NativeError> for KernelError {
    fn from(error: NativeError) -> Self {
        Self::Native(error)
    }
}
impl std::fmt::Display for KernelError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Input { at, problem } => write!(f, "extrusion input {at:?}: {problem:?}"),
            Self::Native(error) => write!(f, "native geometry: {error:?}"),
        }
    }
}
impl std::error::Error for KernelError {}
/// Owned implementation receipt. Native capabilities do not imply Rust operation parity.
/// Identity is diagnostic, not a persisted IR hash or cache key.
#[derive(Debug)]
pub struct KernelInfo {
    pub identity: String,
    pub backend: BackendInfo,
}
/// Thread-affine typed facade. Only extrusion is exposed in this initial migration slice.
///
/// ```compile_fail
/// use nightfall_kernel::Kernel;
/// let kernel = Kernel::new(1e-6, 1e-9).unwrap();
/// std::thread::spawn(move || kernel.info());
/// ```
pub struct Kernel {
    native: Context,
    linear_mm: f64,
}
impl Kernel {
    /// Finite positive mm/radian tolerances, subject to the selected solver's precision floors.
    pub fn new(linear_mm: f64, angular_radians: f64) -> Result<Self, KernelError> {
        Ok(Self {
            native: Context::new(linear_mm, angular_radians)?,
            linear_mm,
        })
    }
    /// Copy an implementation receipt independently of native owner lifetimes.
    pub fn info(&self) -> Result<KernelInfo, KernelError> {
        let backend = self.native.info()?;
        Ok(KernelInfo {
            identity: format!(
                "nightfall-rust/{};policy=extrusion-v1;native={}",
                env!("CARGO_PKG_VERSION"),
                backend.identity
            ),
            backend,
        })
    }
    /// Check finite values, nonempty regions/loops and exact curve endpoint connectivity.
    /// This does not replace native orthonormal-frame, winding or full solid validation.
    pub fn validate_extrusion(&self, input: &Extrusion) -> Result<(), KernelError> {
        for (values, location) in [
            (
                input.frame.origin,
                InputLocation::FrameOrigin as fn(usize) -> InputLocation,
            ),
            (input.frame.x_axis, InputLocation::FrameXAxis),
            (input.frame.normal, InputLocation::FrameNormal),
            (input.displacement, InputLocation::Displacement),
        ] {
            if let Some(axis) = values.iter().position(|v| !v.is_finite()) {
                return Err(invalid(location(axis), InputProblem::NonFinite));
            }
        }
        if input.regions.is_empty() {
            return Err(invalid(InputLocation::Regions, InputProblem::Empty));
        }
        for (r, region) in input.regions.iter().enumerate() {
            for (l, curves) in std::iter::once(&region.outer)
                .chain(&region.holes)
                .enumerate()
            {
                self.validate_loop(curves, r, l)?;
            }
        }
        Ok(())
    }
    /// Exact extrusion; inputs stay immutable, readers retain their originating snapshot.
    pub fn extrude(&self, input: &Extrusion) -> Result<OperationResult, KernelError> {
        self.validate_extrusion(input)?;
        Ok(self.native.extrude(input)?)
    }
    fn validate_loop(
        &self,
        curves: &[NurbsCurve2],
        region: usize,
        loop_index: usize,
    ) -> Result<(), KernelError> {
        if curves.is_empty() {
            return Err(invalid(
                InputLocation::Loop { region, loop_index },
                InputProblem::Empty,
            ));
        }
        let at = |curve| InputLocation::Curve {
            region,
            loop_index,
            curve,
        };
        let point = |i: usize, end: bool| {
            let curve = &curves[i];
            let (start, finish) = curve.knots().domain();
            curve
                .point(if end { finish } else { start })
                .map_err(|error| invalid(at(i), InputProblem::Evaluation(error)))
        };
        for i in 0..curves.len() {
            let end = point(i, true)?;
            let start = point((i + 1) % curves.len(), false)?;
            let gap = (end[0] - start[0]).hypot(end[1] - start[1]);
            if !gap.is_finite() || gap > self.linear_mm {
                return Err(invalid(at(i), InputProblem::Disconnected));
            }
        }
        Ok(())
    }
}
fn invalid(at: InputLocation, problem: InputProblem) -> KernelError {
    KernelError::Input { at, problem }
}
