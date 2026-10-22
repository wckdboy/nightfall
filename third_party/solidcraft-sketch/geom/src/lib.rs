// SPDX-FileCopyrightText: Copyright (c) 2026 SolidCraft contributors
// SPDX-License-Identifier: MIT OR Apache-2.0
// Vendored from SolidCraft 6f5d0038abffbd3e1fc26069b1b96ca89984058f; modified by Nightfall's
// third_party/solidcraft-sketch/vendor.py: serde, thiserror, and modules the sketch solver does not use removed.
//! Geometry foundation for SolidCraft: 2D/3D vectors, sketch planes, axis-aligned boxes,
//! triangle meshes with their measures (volume, area, centroid), and freeform surface maths
//! (B-spline basis and NURBS surfaces in [`bspline`], spline spaces and the operators that make
//! splines compatible in [`spline_space`], one-surface fills of closed loops in [`fill`],
//! surfaces through curve networks in [`network`], T-splines in [`tspline`]). Units are millimetres.
//!
//! This crate has no knowledge of sketches, documents or a modelling backend; every other crate
//! builds on it.
#![deny(clippy::unwrap_used, clippy::expect_used, clippy::panic, clippy::unimplemented, clippy::todo, clippy::unreachable)]
#![forbid(unsafe_code)]

mod aabb2;
mod chain;
mod profile;
mod vec;

pub use aabb2::Aabb2;
pub use chain::{Chain, Closed, Ends, Open, Path, Planar, Ring};
pub use profile::{Loop2, MAX_BEZIER_CTRL, MAX_BEZIER_DEGREE, MAX_DERIVATIVE_ORDER, Region2, Seg2, bezier, regions_apart};
pub use vec::{Vec2, Vec3};

/// Geometric tolerance for coincidence tests (mm).
pub const EPS: f64 = 1e-9;
/// Linear tolerance used for "same point" decisions in modelling (mm).
pub const LINEAR_TOL: f64 = 1e-6;

/// `x` if finite, else `fallback`.
pub fn finite_or(x: f64, fallback: f64) -> f64 {
    if x.is_finite() { x } else { fallback }
}
