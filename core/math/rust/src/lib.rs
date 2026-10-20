// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//! Kernel-free numerical geometry for the Rust migration (RFC 0008).
#![forbid(unsafe_code)]

/// Maximum supported polynomial degree; evaluation workspaces are bounded accordingly.
pub const MAX_DEGREE: usize = 9;
const SIDE: usize = MAX_DEGREE + 1;

mod basis;
mod knot_vector;
pub use basis::{BasisDerivatives, BasisValues, basis_derivatives, basis_functions};
pub use knot_vector::KnotVector;

/// Checked construction/evaluation failure; errors never allocate diagnostic strings.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MathError {
    Degree,
    KnotCount,
    NonFiniteKnot,
    KnotOrder,
    EndMultiplicity,
    InteriorMultiplicity,
    EmptyDomain,
    NonFiniteParameter,
    DerivativeOrder,
    NumericalRange,
}
