// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//! The safe half of `nf_assembly_solve` (`include/nf/assembly/solve.h`): C records to and from
//! `nightfall-assembly`. The pointer handling is in `crate::abi`.

use nightfall_assembly::{
    Assembly, AssemblyError, Frame, Joint, JointKind, Occurrence, Placement, SolveOptions, Status,
    solve,
};

pub const OK: u32 = 0;
pub const INVALID_ARGUMENT: u32 = 1;
pub const INVALID_VALUE: u32 = 2;
pub const INVALID_PLACEMENT: u32 = 3;
pub const DEGENERATE_FRAME: u32 = 4;
pub const UNKNOWN_OCCURRENCE: u32 = 5;
pub const INTERNAL: u32 = 6;

/// `nf_placement`.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct CPlacement {
    pub rotation: [f64; 9],
    pub translation: [f64; 3],
}

/// `nf_frame`.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct CFrame {
    pub origin: [f64; 3],
    pub x: [f64; 3],
    pub y: [f64; 3],
    pub z: [f64; 3],
}

/// `nf_occurrence`.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct COccurrence {
    pub grounded: u32,
    pub reserved: u32,
    pub placement: CPlacement,
}

/// `nf_joint`.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct CJoint {
    pub kind: u32,
    pub a: u32,
    pub b: u32,
    pub reserved: u32,
    pub frame_a: CFrame,
    pub frame_b: CFrame,
    pub driven: [u8; 3],
    pub limited: [u8; 3],
    pub reserved_flags: [u8; 2],
    pub values: [f64; 3],
    pub lower: [f64; 3],
    pub upper: [f64; 3],
}

/// `nf_assembly_report`.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct CReport {
    pub solved: u32,
    pub dof: u32,
    pub loops: u32,
    pub iterations: u32,
}

/// Outputs of [`solve_c`]: one slot per occurrence and per joint.
pub struct Outputs<'a> {
    pub placements: &'a mut [CPlacement],
    pub residual_mm: &'a mut [f64],
    pub residual_rad: &'a mut [f64],
    pub redundant: &'a mut [u8],
    pub conflicting: &'a mut [u8],
    pub report: &'a mut CReport,
}

/// A status code with the occurrence or joint index it concerns (`usize::MAX`: neither).
pub type Failure = (u32, usize);

fn occurrence_id(i: usize) -> String {
    format!("o{i}")
}

fn joint_id(j: usize) -> String {
    format!("j{j}")
}

fn index_of(id: &str) -> Option<usize> {
    id.get(1..).and_then(|n| n.parse().ok())
}

fn placement(p: &CPlacement) -> Placement {
    let r = p.rotation;
    Placement {
        rotation: [[r[0], r[1], r[2]], [r[3], r[4], r[5]], [r[6], r[7], r[8]]],
        translation: p.translation,
    }
}

fn encode(p: &Placement) -> CPlacement {
    let r = p.rotation;
    CPlacement {
        rotation: [
            r[0][0], r[0][1], r[0][2], r[1][0], r[1][1], r[1][2], r[2][0], r[2][1], r[2][2],
        ],
        translation: p.translation,
    }
}

fn frame(f: &CFrame) -> Frame {
    Frame {
        origin: f.origin,
        x: f.x,
        y: f.y,
        z: f.z,
    }
}

fn kind(k: u32) -> Option<JointKind> {
    Some(match k {
        0 => JointKind::Rigid,
        1 => JointKind::Revolute,
        2 => JointKind::Slider,
        3 => JointKind::Cylindrical,
        4 => JointKind::PinSlot,
        5 => JointKind::Planar,
        6 => JointKind::Ball,
        _ => return None,
    })
}

fn failure(e: AssemblyError) -> Failure {
    match e {
        AssemblyError::Empty | AssemblyError::TooLarge => (INVALID_ARGUMENT, usize::MAX),
        AssemblyError::DuplicateOccurrence { .. } | AssemblyError::DuplicateJoint { .. } => {
            (INTERNAL, usize::MAX)
        }
        AssemblyError::InvalidPlacement { occurrence } => (INVALID_PLACEMENT, occurrence),
        AssemblyError::DegenerateFrame { joint } => (DEGENERATE_FRAME, joint),
        AssemblyError::UnknownOccurrence { joint } | AssemblyError::SameOccurrence { joint } => {
            (UNKNOWN_OCCURRENCE, joint)
        }
        AssemblyError::ValueCount { joint } => (INVALID_ARGUMENT, joint),
        AssemblyError::InvalidValue { joint } | AssemblyError::ValueOutsideLimits { joint } => {
            (INVALID_VALUE, joint)
        }
    }
}

/// The safe core of `nf_assembly_solve`.
pub fn solve_c(
    occurrences: &[COccurrence],
    joints: &[CJoint],
    out: Outputs<'_>,
) -> Result<(), Failure> {
    let (n, m) = (occurrences.len(), joints.len());
    if out.placements.len() != n
        || out.residual_mm.len() != m
        || out.residual_rad.len() != m
        || out.redundant.len() != m
        || out.conflicting.len() != m
    {
        return Err((INVALID_ARGUMENT, usize::MAX));
    }
    let mut assembly = Assembly::default();
    for (i, o) in occurrences.iter().enumerate() {
        assembly.occurrences.push(Occurrence {
            id: occurrence_id(i),
            grounded: o.grounded != 0,
            placement: placement(&o.placement),
        });
    }
    for (j, c) in joints.iter().enumerate() {
        let k = kind(c.kind).ok_or((INVALID_ARGUMENT, j))?;
        let (a, b) = (c.a as usize, c.b as usize);
        if a >= n || b >= n {
            return Err((UNKNOWN_OCCURRENCE, j));
        }
        let mut joint = Joint::new(
            &joint_id(j),
            k,
            &occurrence_id(a),
            frame(&c.frame_a),
            &occurrence_id(b),
            frame(&c.frame_b),
        );
        let count = k.value_count();
        for coordinate in 0..count {
            if c.driven[coordinate] != 0 {
                joint.values[coordinate] = Some(c.values[coordinate]);
            }
        }
        if c.limited[..count].iter().any(|&l| l != 0) {
            joint.limits = (0..count)
                .map(|coordinate| {
                    (c.limited[coordinate] != 0)
                        .then_some((c.lower[coordinate], c.upper[coordinate]))
                })
                .collect();
        }
        assembly.joints.push(joint);
    }
    let solution = solve(&assembly, &SolveOptions::default()).map_err(failure)?;
    for (slot, p) in out.placements.iter_mut().zip(&solution.placements) {
        *slot = encode(p);
    }
    out.residual_mm.fill(0.0);
    out.residual_rad.fill(0.0);
    for r in &solution.report.residuals {
        if let Some(j) = index_of(&r.joint).filter(|&j| j < m) {
            out.residual_mm[j] = r.length_mm;
            out.residual_rad[j] = r.angle_rad;
        }
    }
    out.redundant.fill(0);
    out.conflicting.fill(0);
    for (ids, flags) in [
        (&solution.report.redundant, &mut *out.redundant),
        (&solution.report.conflicting, &mut *out.conflicting),
    ] {
        for id in ids {
            if let Some(flag) = index_of(id).and_then(|j| flags.get_mut(j)) {
                *flag = 1;
            }
        }
    }
    *out.report = CReport {
        solved: u32::from(solution.report.status == Status::Solved),
        dof: u32::try_from(solution.report.dof).unwrap_or(u32::MAX),
        loops: u32::try_from(solution.report.loops).unwrap_or(u32::MAX),
        iterations: u32::try_from(solution.report.iterations).unwrap_or(u32::MAX),
    };
    Ok(())
}
