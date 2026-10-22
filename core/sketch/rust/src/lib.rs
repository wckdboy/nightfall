// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//! 2D sketch constraint solving for the feature IR (RFC 0005 sketches, RFC 0012 solver).
//!
//! A [`Sketch`] holds RFC 0005 entities, with their authored coordinates as the starting
//! point, and the v1 constraints between them, with dimensional values already evaluated to
//! millimetres and radians. [`solve`] returns the entities at the solved coordinates and a
//! [`Report`]: degrees of freedom, the entities the constraints do not fully determine, and the
//! constraints that fail or are redundant, by their index in [`Sketch::constraints`].
//!
//! The numerical work is the vendored SolidCraft solver (`third_party/solidcraft-sketch`).
//! This crate maps Nightfall's entity and constraint vocabulary onto it, validates input
//! before any numerical work, and never panics on hostile input.
//!
//! Supported entities: points, lines, circles and arcs. Other RFC 0005 entities (ellipses,
//! rectangles, polygons, splines, equation curves, SVG) stay outside the solver as rigid
//! geometry; a constraint that refers to one is [`Error::UnknownEntity`].
#![forbid(unsafe_code)]
#![deny(
    clippy::unwrap_used,
    clippy::expect_used,
    clippy::panic,
    clippy::unimplemented,
    clippy::todo
)]

use std::collections::HashMap;
use std::f64::consts::TAU;

use solidcraft_geom::Vec2;
use solidcraft_sketch::{ConstraintKind, CurveKind, SolveStatus};

/// A point in sketch coordinates, millimetres.
pub type Point = [f64; 2];

/// An RFC 0005 sketch entity that takes part in solving.
#[derive(Clone, Debug, PartialEq)]
pub enum Entity {
    Point {
        at: Point,
    },
    Line {
        start: Point,
        end: Point,
    },
    Circle {
        center: Point,
        radius: f64,
    },
    /// Counter-clockwise for a positive sweep, clockwise for a negative one; radians.
    Arc {
        center: Point,
        radius: f64,
        start_angle: f64,
        sweep_angle: f64,
    },
}

/// Which point of an entity a reference means (RFC 0005 `local_entity`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Role {
    Start,
    End,
    Center,
}

/// A reference to an entity, or to one of its points.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Ref {
    pub entity: String,
    pub point: Option<Role>,
}

impl Ref {
    /// The whole entity.
    pub fn entity(id: &str) -> Self {
        Ref {
            entity: id.into(),
            point: None,
        }
    }
    /// One point of an entity.
    pub fn point(id: &str, role: Role) -> Self {
        Ref {
            entity: id.into(),
            point: Some(role),
        }
    }
}

/// The RFC 0005 v1 constraints. Values are millimetres and radians.
#[derive(Clone, Debug, PartialEq)]
pub enum Constraint {
    /// Two points, or a point and a curve it lies on.
    Coincident(Ref, Ref),
    Horizontal(Ref),
    Vertical(Ref),
    Parallel(Ref, Ref),
    Perpendicular(Ref, Ref),
    Tangent(Ref, Ref),
    /// Equal lengths of two lines, or equal radii of two circles or arcs.
    Equal(Ref, Ref),
    /// Two points mirror each other across a line: `(a, b, axis)`.
    Symmetric(Ref, Ref, Ref),
    /// A point at the middle of a line, in either order.
    Midpoint(Ref, Ref),
    /// A point, or every defining point (and a circle's radius) of a curve, stays put.
    Fixed(Ref),
    /// Between two points, or from a point to a line.
    Distance(Ref, Ref, f64),
    Angle(Ref, Ref, f64),
    Radius(Ref, f64),
}

/// Entities by id, in authored order, and the constraints between them.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Sketch {
    pub entities: Vec<(String, Entity)>,
    pub constraints: Vec<Constraint>,
}

/// Whether every constraint holds.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Status {
    /// Every constraint holds.
    Solved,
    /// The constraints conflict or could not be satisfied; the entities are as authored.
    Failed,
}

/// What the solve found. Constraint references are indices into [`Sketch::constraints`].
#[derive(Clone, Debug, PartialEq)]
pub struct Report {
    pub status: Status,
    /// Remaining degrees of freedom of the whole sketch.
    pub dof: usize,
    /// Entities whose position or size the constraints do not determine, in authored order.
    pub underconstrained: Vec<String>,
    /// Constraints still violated when the solve failed.
    pub failing: Vec<usize>,
    /// Constraints that hold but add no information.
    pub redundant: Vec<usize>,
    /// Largest constraint residual at the result, millimetres or radians.
    pub max_residual: f64,
    pub iterations: usize,
}

/// The entities at the solved coordinates (or as authored after a failure) and the report.
#[derive(Clone, Debug, PartialEq)]
pub struct Solution {
    pub entities: Vec<(String, Entity)>,
    pub report: Report,
}

/// Input problems, found before any numerical work.
#[derive(Clone, Debug, PartialEq)]
pub enum Error {
    /// A coordinate, radius, angle or constraint value is not finite or out of range.
    InvalidValue { what: String },
    /// Two entities share an id.
    DuplicateId(String),
    /// A constraint names an entity that does not exist (constraint index, entity id).
    UnknownEntity(usize, String),
    /// A constraint does not apply to the entities it names (constraint index, reason).
    Unsupported(usize, String),
    /// The solver refused the geometry (a degenerate line, arc or circle).
    Geometry(String),
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Error::InvalidValue { what } => write!(f, "invalid value: {what}"),
            Error::DuplicateId(id) => write!(f, "duplicate entity id `{id}`"),
            Error::UnknownEntity(i, id) => write!(f, "constraint {i}: unknown entity `{id}`"),
            Error::Unsupported(i, why) => write!(f, "constraint {i}: {why}"),
            Error::Geometry(why) => write!(f, "geometry: {why}"),
        }
    }
}

impl std::error::Error for Error {}

/// Where each Nightfall entity lives in the solver's sketch.
#[derive(Clone, Copy)]
enum Slot {
    Point(usize),
    Line {
        curve: usize,
        start: usize,
        end: usize,
    },
    Circle {
        curve: usize,
        center: usize,
    },
    /// `reversed` when a clockwise arc is stored counter-clockwise (start and end swapped).
    Arc {
        curve: usize,
        center: usize,
        start: usize,
        end: usize,
        reversed: bool,
    },
}

fn finite(v: f64, what: impl FnOnce() -> String) -> Result<f64, Error> {
    if v.is_finite() {
        Ok(v)
    } else {
        Err(Error::InvalidValue { what: what() })
    }
}

fn positive(v: f64, what: impl FnOnce() -> String) -> Result<f64, Error> {
    if v.is_finite() && v > 0.0 {
        Ok(v)
    } else {
        Err(Error::InvalidValue { what: what() })
    }
}

fn point(p: Point, what: &str) -> Result<Vec2, Error> {
    Ok(Vec2::new(
        finite(p[0], || format!("{what} x"))?,
        finite(p[1], || format!("{what} y"))?,
    ))
}

const NAN2: Point = [f64::NAN, f64::NAN];

/// Shortest line accepted as input, millimetres; anything shorter cannot bound a profile.
const MIN_LENGTH: f64 = 1e-9;

struct Model {
    sk: solidcraft_sketch::Sketch,
    slots: HashMap<String, Slot>,
}

impl Model {
    fn build(sketch: &Sketch) -> Result<Self, Error> {
        let mut model = Model {
            sk: solidcraft_sketch::Sketch::new(),
            slots: HashMap::new(),
        };
        let geometry = |e: solidcraft_sketch::SketchError| Error::Geometry(e.to_string());
        for (id, entity) in &sketch.entities {
            if model.slots.contains_key(id) {
                return Err(Error::DuplicateId(id.clone()));
            }
            let created = |m: &Model, curve: usize| m.sk.curves.get(curve).map(|c| c.kind.clone());
            let slot = match entity {
                Entity::Point { at } => Slot::Point(
                    model
                        .sk
                        .add_point(point(*at, id)?, None)
                        .map_err(geometry)?,
                ),
                Entity::Line { start, end } => {
                    if (start[0] - end[0]).hypot(start[1] - end[1]) <= MIN_LENGTH {
                        return Err(Error::InvalidValue {
                            what: format!("{id} has zero length"),
                        });
                    }
                    let curve = model
                        .sk
                        .add_line(point(*start, id)?, point(*end, id)?, None, None, None)
                        .map_err(geometry)?;
                    match created(&model, curve) {
                        Some(CurveKind::Line { a, b }) => Slot::Line {
                            curve,
                            start: a,
                            end: b,
                        },
                        _ => return Err(Error::Geometry(format!("line `{id}` was not created"))),
                    }
                }
                Entity::Circle { center, radius } => {
                    let r = positive(*radius, || format!("{id} radius"))?;
                    let curve = model
                        .sk
                        .add_circle(point(*center, id)?, r, None, None)
                        .map_err(geometry)?;
                    match created(&model, curve) {
                        Some(CurveKind::Circle { c, .. }) => Slot::Circle { curve, center: c },
                        _ => return Err(Error::Geometry(format!("circle `{id}` was not created"))),
                    }
                }
                Entity::Arc {
                    center,
                    radius,
                    start_angle,
                    sweep_angle,
                } => {
                    let r = positive(*radius, || format!("{id} radius"))?;
                    let s = finite(*start_angle, || format!("{id} start angle"))?;
                    let w = finite(*sweep_angle, || format!("{id} sweep angle"))?;
                    if w == 0.0 || w.abs() >= TAU {
                        return Err(Error::InvalidValue {
                            what: format!("{id} sweep angle (0 < |sweep| < 2π)"),
                        });
                    }
                    let c = point(*center, id)?;
                    let at = |a: f64| Vec2::new(c.x + r * a.cos(), c.y + r * a.sin());
                    let reversed = w < 0.0;
                    let (from, to) = if reversed {
                        (at(s + w), at(s))
                    } else {
                        (at(s), at(s + w))
                    };
                    let curve = model
                        .sk
                        .add_arc(c, from, to, [None, None, None], None)
                        .map_err(geometry)?;
                    match created(&model, curve) {
                        Some(CurveKind::Arc { c: center, a, b }) => {
                            let (start, end) = if reversed { (b, a) } else { (a, b) };
                            Slot::Arc {
                                curve,
                                center,
                                start,
                                end,
                                reversed,
                            }
                        }
                        _ => return Err(Error::Geometry(format!("arc `{id}` was not created"))),
                    }
                }
            };
            model.slots.insert(id.clone(), slot);
        }
        Ok(model)
    }

    fn slot(&self, i: usize, r: &Ref) -> Result<Slot, Error> {
        self.slots
            .get(&r.entity)
            .copied()
            .ok_or_else(|| Error::UnknownEntity(i, r.entity.clone()))
    }

    fn unsupported<T>(i: usize, r: &Ref, why: &str) -> Result<T, Error> {
        Err(Error::Unsupported(i, format!("`{}`: {why}", r.entity)))
    }

    /// The solver point a reference names: a point entity, or a role of a curve.
    fn point_of(&self, i: usize, r: &Ref) -> Result<usize, Error> {
        match (self.slot(i, r)?, r.point) {
            (Slot::Point(p), None) => Ok(p),
            (Slot::Line { start, .. } | Slot::Arc { start, .. }, Some(Role::Start)) => Ok(start),
            (Slot::Line { end, .. } | Slot::Arc { end, .. }, Some(Role::End)) => Ok(end),
            (Slot::Circle { center, .. } | Slot::Arc { center, .. }, Some(Role::Center)) => {
                Ok(center)
            }
            (Slot::Point(_), Some(_)) => {
                Self::unsupported(i, r, "a point has no start, end or center")
            }
            (_, None) => {
                Self::unsupported(i, r, "expected a point; name one with start, end or center")
            }
            (Slot::Line { .. }, Some(Role::Center)) => {
                Self::unsupported(i, r, "a line has no center")
            }
            (Slot::Circle { .. }, Some(_)) => Self::unsupported(i, r, "a circle has only a center"),
        }
    }

    /// The solver curve a reference names (a whole line, circle or arc).
    fn curve_of(&self, i: usize, r: &Ref) -> Result<usize, Error> {
        match (self.slot(i, r)?, r.point) {
            (
                Slot::Line { curve, .. } | Slot::Circle { curve, .. } | Slot::Arc { curve, .. },
                None,
            ) => Ok(curve),
            _ => Self::unsupported(i, r, "expected a whole line, circle or arc"),
        }
    }

    fn line_of(&self, i: usize, r: &Ref) -> Result<usize, Error> {
        match (self.slot(i, r)?, r.point) {
            (Slot::Line { curve, .. }, None) => Ok(curve),
            _ => Self::unsupported(i, r, "expected a line"),
        }
    }

    fn is_point(&self, r: &Ref) -> bool {
        r.point.is_some() || matches!(self.slots.get(&r.entity), Some(Slot::Point(_)))
    }

    fn kinds(&self, i: usize, c: &Constraint) -> Result<Vec<ConstraintKind>, Error> {
        let value = |v: f64| finite(v, || format!("constraint {i} value"));
        Ok(match c {
            Constraint::Coincident(a, b) => match (self.is_point(a), self.is_point(b)) {
                (true, true) => vec![ConstraintKind::Coincident {
                    p: self.point_of(i, a)?,
                    q: self.point_of(i, b)?,
                }],
                (true, false) => vec![ConstraintKind::PointOnCurve {
                    p: self.point_of(i, a)?,
                    c: self.curve_of(i, b)?,
                }],
                (false, true) => vec![ConstraintKind::PointOnCurve {
                    p: self.point_of(i, b)?,
                    c: self.curve_of(i, a)?,
                }],
                (false, false) => {
                    return Err(Error::Unsupported(
                        i,
                        "coincident needs at least one point".into(),
                    ));
                }
            },
            Constraint::Horizontal(l) => vec![ConstraintKind::Horizontal {
                l: self.line_of(i, l)?,
            }],
            Constraint::Vertical(l) => vec![ConstraintKind::Vertical {
                l: self.line_of(i, l)?,
            }],
            Constraint::Parallel(a, b) => vec![ConstraintKind::Parallel {
                a: self.line_of(i, a)?,
                b: self.line_of(i, b)?,
            }],
            Constraint::Perpendicular(a, b) => {
                vec![ConstraintKind::Perpendicular {
                    a: self.line_of(i, a)?,
                    b: self.line_of(i, b)?,
                }]
            }
            Constraint::Tangent(a, b) => vec![ConstraintKind::Tangent {
                a: self.curve_of(i, a)?,
                b: self.curve_of(i, b)?,
            }],
            Constraint::Equal(a, b) => vec![ConstraintKind::Equal {
                a: self.curve_of(i, a)?,
                b: self.curve_of(i, b)?,
            }],
            Constraint::Symmetric(a, b, axis) => {
                vec![ConstraintKind::Symmetric {
                    p: self.point_of(i, a)?,
                    q: self.point_of(i, b)?,
                    l: self.line_of(i, axis)?,
                }]
            }
            Constraint::Midpoint(a, b) => {
                let (p, l) = if self.is_point(a) { (a, b) } else { (b, a) };
                vec![ConstraintKind::Midpoint {
                    p: self.point_of(i, p)?,
                    l: self.line_of(i, l)?,
                }]
            }
            Constraint::Fixed(r) if self.is_point(r) => vec![ConstraintKind::Fix {
                p: self.point_of(i, r)?,
            }],
            Constraint::Fixed(r) => match self.slot(i, r)? {
                Slot::Point(p) => vec![ConstraintKind::Fix { p }],
                Slot::Line { start, end, .. } => vec![
                    ConstraintKind::Fix { p: start },
                    ConstraintKind::Fix { p: end },
                ],
                Slot::Circle { curve, center } => {
                    let radius = self.sk.radius(curve).unwrap_or(f64::NAN);
                    vec![
                        ConstraintKind::Fix { p: center },
                        ConstraintKind::Radius {
                            c: curve,
                            value: radius,
                        },
                    ]
                }
                Slot::Arc {
                    center, start, end, ..
                } => {
                    vec![
                        ConstraintKind::Fix { p: center },
                        ConstraintKind::Fix { p: start },
                        ConstraintKind::Fix { p: end },
                    ]
                }
            },
            Constraint::Distance(a, b, v) => {
                let v = value(*v)?;
                match (self.is_point(a), self.is_point(b)) {
                    (true, true) => vec![ConstraintKind::Distance {
                        p: self.point_of(i, a)?,
                        q: self.point_of(i, b)?,
                        value: v,
                    }],
                    (true, false) => {
                        vec![ConstraintKind::PointLineDistance {
                            p: self.point_of(i, a)?,
                            l: self.line_of(i, b)?,
                            value: v,
                        }]
                    }
                    (false, true) => {
                        vec![ConstraintKind::PointLineDistance {
                            p: self.point_of(i, b)?,
                            l: self.line_of(i, a)?,
                            value: v,
                        }]
                    }
                    (false, false) => {
                        return Err(Error::Unsupported(
                            i,
                            "distance needs a point, and a point or a line".into(),
                        ));
                    }
                }
            }
            Constraint::Angle(a, b, v) => {
                vec![ConstraintKind::Angle {
                    a: self.line_of(i, a)?,
                    b: self.line_of(i, b)?,
                    value: value(*v)?,
                    flip: false,
                }]
            }
            Constraint::Radius(c, v) => {
                let v = positive(*v, || format!("constraint {i} radius"))?;
                match (self.slot(i, c)?, c.point) {
                    (Slot::Circle { curve, .. } | Slot::Arc { curve, .. }, None) => {
                        vec![ConstraintKind::Radius { c: curve, value: v }]
                    }
                    _ => return Self::unsupported(i, c, "expected a circle or arc"),
                }
            }
        })
    }

    fn read(&self, id: &str, original: &Entity) -> Entity {
        let at = |p: usize| self.sk.point(p).map_or(NAN2, |v| [v.x, v.y]);
        match self.slots.get(id).copied() {
            Some(Slot::Point(p)) => Entity::Point { at: at(p) },
            Some(Slot::Line { start, end, .. }) => Entity::Line {
                start: at(start),
                end: at(end),
            },
            Some(Slot::Circle { curve, center }) => Entity::Circle {
                center: at(center),
                radius: self.sk.radius(curve).unwrap_or(f64::NAN),
            },
            Some(Slot::Arc {
                curve,
                center,
                start,
                end,
                reversed,
            }) => {
                let c = at(center);
                let angle = |p: Point| (p[1] - c[1]).atan2(p[0] - c[0]);
                let ccw = (angle(at(end)) - angle(at(start))).rem_euclid(TAU);
                let sweep = if reversed { ccw - TAU } else { ccw };
                Entity::Arc {
                    center: c,
                    radius: self.sk.radius(curve).unwrap_or(f64::NAN),
                    start_angle: angle(at(start)),
                    sweep_angle: sweep,
                }
            }
            None => original.clone(),
        }
    }
}

/// Solves `sketch` from its authored coordinates. A failed solve is a [`Report`] with
/// [`Status::Failed`] and the entities as authored; invalid input is an [`Error`].
pub fn solve(sketch: &Sketch) -> Result<Solution, Error> {
    let mut model = Model::build(sketch)?;
    let mut owner: HashMap<String, usize> = HashMap::new();
    for (i, c) in sketch.constraints.iter().enumerate() {
        for kind in model.kinds(i, c)? {
            let id = model
                .sk
                .add_constraint(kind, None)
                .map_err(|e| Error::Unsupported(i, e.to_string()))?;
            owner.insert(id, i);
        }
    }
    let report = solidcraft_sketch::solve(&mut model.sk);
    let indices = |ids: &[String]| {
        let mut v: Vec<usize> = ids.iter().filter_map(|id| owner.get(id).copied()).collect();
        v.sort_unstable();
        v.dedup();
        v
    };
    let point_known = |i: usize| report.point_determined.get(i).copied().unwrap_or(false);
    let curve_known = |i: usize| report.curve_determined.get(i).copied().unwrap_or(false);
    let determined = |slot: Slot| match slot {
        Slot::Point(p) => point_known(p),
        Slot::Line { start, end, .. } => point_known(start) && point_known(end),
        Slot::Circle { curve, center } => point_known(center) && curve_known(curve),
        Slot::Arc {
            curve,
            center,
            start,
            end,
            ..
        } => point_known(center) && point_known(start) && point_known(end) && curve_known(curve),
    };
    let entities = sketch
        .entities
        .iter()
        .map(|(id, e)| (id.clone(), model.read(id, e)))
        .collect();
    let underconstrained = sketch
        .entities
        .iter()
        .filter(|(id, _)| model.slots.get(id).is_some_and(|s| !determined(*s)))
        .map(|(id, _)| id.clone())
        .collect();
    Ok(Solution {
        entities,
        report: Report {
            status: match report.status {
                SolveStatus::Solved => Status::Solved,
                SolveStatus::Failed => Status::Failed,
            },
            dof: report.dof,
            underconstrained,
            failing: indices(&report.failing),
            redundant: indices(&report.redundant),
            max_residual: report.max_residual,
            iterations: report.iterations,
        },
    })
}
