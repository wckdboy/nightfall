// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//! The safe half of `nf_sketch_solve` (`include/nf/sketch/solve.h`): C records to and from
//! `nightfall-sketch`. The pointer handling is in `crate::abi`.

use nightfall_sketch::{Constraint, Entity, Error, Ref, Role, Sketch, Status, solve};

pub const POINT: u32 = 1;
pub const LINE: u32 = 2;
pub const CIRCLE: u32 = 3;
pub const ARC: u32 = 4;

pub const WHOLE: u32 = 0;
pub const START: u32 = 1;
pub const END: u32 = 2;
pub const CENTER: u32 = 3;

pub const COINCIDENT: u32 = 1;
pub const HORIZONTAL: u32 = 2;
pub const VERTICAL: u32 = 3;
pub const PARALLEL: u32 = 4;
pub const PERPENDICULAR: u32 = 5;
pub const TANGENT: u32 = 6;
pub const EQUAL: u32 = 7;
pub const SYMMETRIC: u32 = 8;
pub const MIDPOINT: u32 = 9;
pub const FIXED: u32 = 10;
pub const DISTANCE: u32 = 11;
pub const ANGLE: u32 = 12;
pub const RADIUS: u32 = 13;

pub const OK: u32 = 0;
pub const INVALID_ARGUMENT: u32 = 1;
pub const INVALID_VALUE: u32 = 2;
pub const UNKNOWN_ENTITY: u32 = 3;
pub const UNSUPPORTED: u32 = 4;
pub const GEOMETRY: u32 = 5;
pub const INTERNAL: u32 = 6;

/// `nf_sketch_entity`.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct CEntity {
    pub kind: u32,
    pub reserved: u32,
    pub values: [f64; 5],
}

/// `nf_sketch_ref`.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct CRef {
    pub entity: u32,
    pub role: u32,
}

/// `nf_sketch_constraint`.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct CConstraint {
    pub kind: u32,
    pub ref_count: u32,
    pub refs: [CRef; 3],
    pub value: f64,
}

/// `nf_sketch_report`.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct CReport {
    pub solved: u32,
    pub dof: u32,
    pub iterations: u32,
    pub reserved: u32,
    pub max_residual: f64,
}

/// Outputs of [`solve_c`], one slot per entity and per constraint.
pub struct Outputs<'a> {
    pub solved: &'a mut [CEntity],
    pub underconstrained: &'a mut [u8],
    pub failing: &'a mut [u8],
    pub redundant: &'a mut [u8],
    pub report: &'a mut CReport,
}

/// A status code with the entity or constraint index it concerns (`usize::MAX`: neither).
pub type Failure = (u32, usize);

fn id(i: usize) -> String {
    format!("e{i}")
}

fn index_of(id: &str) -> usize {
    id.strip_prefix('e')
        .and_then(|n| n.parse().ok())
        .unwrap_or(usize::MAX)
}

fn entity(i: usize, e: &CEntity) -> Result<Entity, Failure> {
    let v = e.values;
    match e.kind {
        POINT => Ok(Entity::Point { at: [v[0], v[1]] }),
        LINE => Ok(Entity::Line {
            start: [v[0], v[1]],
            end: [v[2], v[3]],
        }),
        CIRCLE => Ok(Entity::Circle {
            center: [v[0], v[1]],
            radius: v[2],
        }),
        ARC => Ok(Entity::Arc {
            center: [v[0], v[1]],
            radius: v[2],
            start_angle: v[3],
            sweep_angle: v[4],
        }),
        _ => Err((INVALID_ARGUMENT, i)),
    }
}

fn reference(j: usize, entities: usize, r: &CRef) -> Result<Ref, Failure> {
    if r.entity as usize >= entities {
        return Err((UNKNOWN_ENTITY, j));
    }
    let point = match r.role {
        WHOLE => None,
        START => Some(Role::Start),
        END => Some(Role::End),
        CENTER => Some(Role::Center),
        _ => return Err((INVALID_ARGUMENT, j)),
    };
    Ok(Ref {
        entity: id(r.entity as usize),
        point,
    })
}

fn constraint(j: usize, entities: usize, c: &CConstraint) -> Result<Constraint, Failure> {
    let needed = match c.kind {
        HORIZONTAL | VERTICAL | FIXED | RADIUS => 1,
        COINCIDENT | PARALLEL | PERPENDICULAR | TANGENT | EQUAL | MIDPOINT | DISTANCE | ANGLE => 2,
        SYMMETRIC => 3,
        _ => return Err((INVALID_ARGUMENT, j)),
    };
    if c.ref_count != needed {
        return Err((INVALID_ARGUMENT, j));
    }
    let r = |k: usize| reference(j, entities, &c.refs[k]);
    Ok(match c.kind {
        COINCIDENT => Constraint::Coincident(r(0)?, r(1)?),
        HORIZONTAL => Constraint::Horizontal(r(0)?),
        VERTICAL => Constraint::Vertical(r(0)?),
        PARALLEL => Constraint::Parallel(r(0)?, r(1)?),
        PERPENDICULAR => Constraint::Perpendicular(r(0)?, r(1)?),
        TANGENT => Constraint::Tangent(r(0)?, r(1)?),
        EQUAL => Constraint::Equal(r(0)?, r(1)?),
        SYMMETRIC => Constraint::Symmetric(r(0)?, r(1)?, r(2)?),
        MIDPOINT => Constraint::Midpoint(r(0)?, r(1)?),
        FIXED => Constraint::Fixed(r(0)?),
        DISTANCE => Constraint::Distance(r(0)?, r(1)?, c.value),
        ANGLE => Constraint::Angle(r(0)?, r(1)?, c.value),
        _ => Constraint::Radius(r(0)?, c.value),
    })
}

fn encode(e: &Entity) -> CEntity {
    let (kind, values) = match *e {
        Entity::Point { at } => (POINT, [at[0], at[1], 0.0, 0.0, 0.0]),
        Entity::Line { start, end } => (LINE, [start[0], start[1], end[0], end[1], 0.0]),
        Entity::Circle { center, radius } => (CIRCLE, [center[0], center[1], radius, 0.0, 0.0]),
        Entity::Arc {
            center,
            radius,
            start_angle,
            sweep_angle,
        } => (
            ARC,
            [center[0], center[1], radius, start_angle, sweep_angle],
        ),
    };
    CEntity {
        kind,
        reserved: 0,
        values,
    }
}

/// The safe core of `nf_sketch_solve`: validates, solves and fills `out`. Output slices must
/// have one slot per entity and per constraint.
pub fn solve_c(
    entities: &[CEntity],
    constraints: &[CConstraint],
    out: Outputs<'_>,
) -> Result<(), Failure> {
    if out.solved.len() != entities.len()
        || out.underconstrained.len() != entities.len()
        || out.failing.len() != constraints.len()
        || out.redundant.len() != constraints.len()
        || entities.len() > u32::MAX as usize
    {
        return Err((INVALID_ARGUMENT, usize::MAX));
    }
    let sketch = Sketch {
        entities: entities
            .iter()
            .enumerate()
            .map(|(i, e)| entity(i, e).map(|e| (id(i), e)))
            .collect::<Result<_, _>>()?,
        constraints: constraints
            .iter()
            .enumerate()
            .map(|(j, c)| constraint(j, entities.len(), c))
            .collect::<Result<_, _>>()?,
    };
    let solution = solve(&sketch).map_err(|e| match e {
        Error::InvalidValue {
            entity: Some(id), ..
        } => (INVALID_VALUE, index_of(&id)),
        Error::InvalidValue { entity: None, what } => {
            // "constraint {j} ..." names the constraint.
            let j = what
                .split_whitespace()
                .nth(1)
                .and_then(|n| n.parse().ok())
                .unwrap_or(usize::MAX);
            (INVALID_VALUE, j)
        }
        Error::DuplicateId(_) => (INTERNAL, usize::MAX),
        Error::UnknownEntity(j, _) => (UNKNOWN_ENTITY, j),
        Error::Unsupported(j, _) => (UNSUPPORTED, j),
        Error::Geometry(_) => (GEOMETRY, usize::MAX),
    })?;
    for (slot, (_, e)) in out.solved.iter_mut().zip(&solution.entities) {
        *slot = encode(e);
    }
    out.underconstrained.fill(0);
    for id in &solution.report.underconstrained {
        if let Some(flag) = out.underconstrained.get_mut(index_of(id)) {
            *flag = 1;
        }
    }
    out.failing.fill(0);
    out.redundant.fill(0);
    for &j in &solution.report.failing {
        if let Some(flag) = out.failing.get_mut(j) {
            *flag = 1;
        }
    }
    for &j in &solution.report.redundant {
        if let Some(flag) = out.redundant.get_mut(j) {
            *flag = 1;
        }
    }
    *out.report = CReport {
        solved: u32::from(solution.report.status == Status::Solved),
        dof: u32::try_from(solution.report.dof).unwrap_or(u32::MAX),
        iterations: u32::try_from(solution.report.iterations).unwrap_or(u32::MAX),
        reserved: 0,
        max_residual: solution.report.max_residual,
    };
    Ok(())
}
