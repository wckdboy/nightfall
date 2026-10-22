// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//! Assembly placement solver (RFC 0013): rigid occurrences joined by frame-based joints.
//!
//! Each joint relates a frame on one occurrence to a frame on another and leaves some of their
//! six relative degrees of freedom free. The solver moves every non-grounded occurrence by an
//! SE(3) increment until the constrained components vanish, starting from the current
//! placements so a solution keeps its branch through edits. Units are millimetres and radians.
#![forbid(unsafe_code)]
#![cfg_attr(
    not(test),
    deny(clippy::unwrap_used, clippy::expect_used, clippy::panic)
)]

mod geometry;
mod solver;
pub use geometry::{Frame, Placement, Vec3};
pub use solver::{
    Assembly, Joint, JointKind, JointResidual, Occurrence, Report, Solution, SolveOptions, Status,
    joint_coordinates, solve,
};

/// Invalid input; nothing is solved. Indices refer to the assembly's occurrence or joint lists.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AssemblyError {
    /// The assembly has no occurrences.
    Empty,
    /// More than [`MAX_OCCURRENCES`] occurrences or [`MAX_JOINTS`] joints.
    TooLarge,
    /// Two occurrences share an id.
    DuplicateOccurrence { index: usize },
    /// Two joints share an id.
    DuplicateJoint { index: usize },
    /// A placement is not finite or not a proper rotation.
    InvalidPlacement { occurrence: usize },
    /// A joint frame is not finite, orthonormal and right-handed.
    DegenerateFrame { joint: usize },
    /// A joint names an occurrence that does not exist.
    UnknownOccurrence { joint: usize },
    /// Both ends of a joint are on the same occurrence.
    SameOccurrence { joint: usize },
    /// `values` or `limits` does not have one entry per free degree of freedom.
    ValueCount { joint: usize },
    /// A value or limit is not finite, or a limit has its lower bound above its upper bound.
    InvalidValue { joint: usize },
    /// A driven value lies outside its own limits.
    ValueOutsideLimits { joint: usize },
}

impl std::fmt::Display for AssemblyError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Empty => write!(f, "the assembly has no occurrences"),
            Self::TooLarge => write!(f, "the assembly exceeds the occurrence or joint limit"),
            Self::DuplicateOccurrence { index } => write!(f, "occurrence {index} repeats an id"),
            Self::DuplicateJoint { index } => write!(f, "joint {index} repeats an id"),
            Self::InvalidPlacement { occurrence } => {
                write!(f, "occurrence {occurrence} has an invalid placement")
            }
            Self::DegenerateFrame { joint } => write!(f, "joint {joint} has a degenerate frame"),
            Self::UnknownOccurrence { joint } => {
                write!(f, "joint {joint} names an unknown occurrence")
            }
            Self::SameOccurrence { joint } => {
                write!(f, "joint {joint} joins an occurrence to itself")
            }
            Self::ValueCount { joint } => {
                write!(f, "joint {joint} has the wrong number of values or limits")
            }
            Self::InvalidValue { joint } => {
                write!(f, "joint {joint} has an invalid value or limit")
            }
            Self::ValueOutsideLimits { joint } => {
                write!(f, "joint {joint} drives a value outside its limits")
            }
        }
    }
}

impl std::error::Error for AssemblyError {}

/// Bounds that keep the dense solver's work small and predictable.
pub const MAX_OCCURRENCES: usize = 256;
pub const MAX_JOINTS: usize = 512;
