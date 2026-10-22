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
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
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
    /// A coordinate, radius, angle or constraint value is not finite or out of range. `entity`
    /// names the entity, or is `None` for a constraint value.
    InvalidValue {
        entity: Option<String>,
        what: String,
    },
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
            Error::InvalidValue { what, .. } => write!(f, "invalid value: {what}"),
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

fn invalid(entity: Option<&str>, what: String) -> Error {
    Error::InvalidValue {
        entity: entity.map(str::to_owned),
        what,
    }
}

fn finite(v: f64, entity: Option<&str>, what: impl FnOnce() -> String) -> Result<f64, Error> {
    if v.is_finite() {
        Ok(v)
    } else {
        Err(invalid(entity, what()))
    }
}

fn positive(v: f64, entity: Option<&str>, what: impl FnOnce() -> String) -> Result<f64, Error> {
    if v.is_finite() && v > 0.0 {
        Ok(v)
    } else {
        Err(invalid(entity, what()))
    }
}

fn point(p: Point, id: &str) -> Result<Vec2, Error> {
    Ok(Vec2::new(
        finite(p[0], Some(id), || format!("{id} x"))?,
        finite(p[1], Some(id), || format!("{id} y"))?,
    ))
}

const NAN2: Point = [f64::NAN, f64::NAN];

/// Shortest line accepted as input, millimetres; anything shorter cannot bound a profile.
const MIN_LENGTH: f64 = 1e-9;

struct Model {
    sk: solidcraft_sketch::Sketch,
    slots: HashMap<String, Slot>,
}

/// A defining point of an entity: its id and role (`None` for a point entity).
type Key = (String, Option<Role>);

/// The defining points of an entity and their authored positions.
fn point_keys(id: &str, entity: &Entity) -> Vec<(Key, Point)> {
    let key = |role| (id.to_owned(), role);
    match entity {
        Entity::Point { at } => vec![(key(None), *at)],
        Entity::Line { start, end } => vec![
            (key(Some(Role::Start)), *start),
            (key(Some(Role::End)), *end),
        ],
        Entity::Circle { center, .. } => vec![(key(Some(Role::Center)), *center)],
        Entity::Arc {
            center,
            radius,
            start_angle,
            sweep_angle,
        } => {
            let at = |a: f64| [center[0] + radius * a.cos(), center[1] + radius * a.sin()];
            vec![
                (key(Some(Role::Center)), *center),
                (key(Some(Role::Start)), at(*start_angle)),
                (key(Some(Role::End)), at(start_angle + sweep_angle)),
            ]
        }
    }
}

/// Points joined by point-to-point coincidences, which become one solver point. SolidCraft
/// measures tangency at a shared end point linearly (the radius is perpendicular to the line
/// there); between separate points tied by a coincidence it falls back to the distance from
/// the centre, which is quadratic near tangency and leaves the touch point about 5e-5 mm off
/// for a 1e-10 residual. Two fixed classes, or two points of one entity, are never merged, so
/// conflicts and zero-length curves are still reported by their own constraints.
struct Shared {
    /// Each merged point's class; points absent here are not shared.
    class: HashMap<Key, usize>,
    /// Where a class's solver point starts: a fixed member's authored position, if any.
    start: HashMap<usize, Point>,
}

impl Shared {
    fn find(parent: &mut [usize], mut i: usize) -> usize {
        while parent[i] != i {
            parent[i] = parent[parent[i]];
            i = parent[i];
        }
        i
    }

    fn new(sketch: &Sketch) -> Self {
        let mut keys: Vec<(Key, Point)> = Vec::new();
        let mut entity_of: HashMap<&str, &Entity> = HashMap::new();
        for (id, entity) in &sketch.entities {
            entity_of.insert(id, entity);
            keys.extend(point_keys(id, entity));
        }
        let index: HashMap<Key, usize> = keys
            .iter()
            .enumerate()
            .map(|(i, (k, _))| (k.clone(), i))
            .collect();
        let key_of = |r: &Ref| -> Option<usize> {
            match (entity_of.get(r.entity.as_str()), r.point) {
                (Some(Entity::Point { .. }), None) => index.get(&(r.entity.clone(), None)).copied(),
                (Some(_), Some(role)) => index.get(&(r.entity.clone(), Some(role))).copied(),
                _ => None,
            }
        };
        let mut fixed = vec![false; keys.len()];
        for c in &sketch.constraints {
            if let Constraint::Fixed(r) = c {
                match key_of(r) {
                    Some(k) => fixed[k] = true,
                    None if r.point.is_none() => {
                        for (i, ((id, _), _)) in keys.iter().enumerate() {
                            if *id == r.entity {
                                fixed[i] = true;
                            }
                        }
                    }
                    None => {}
                }
            }
        }
        let mut parent: Vec<usize> = (0..keys.len()).collect();
        for c in &sketch.constraints {
            let Constraint::Coincident(a, b) = c else {
                continue;
            };
            let (Some(a), Some(b)) = (key_of(a), key_of(b)) else {
                continue;
            };
            let (ra, rb) = (Self::find(&mut parent, a), Self::find(&mut parent, b));
            if ra == rb {
                continue;
            }
            let members = |parent: &mut [usize], root: usize| -> Vec<usize> {
                (0..parent.len())
                    .filter(|&i| Self::find(parent, i) == root)
                    .collect()
            };
            let (ma, mb) = (members(&mut parent, ra), members(&mut parent, rb));
            let any_fixed = |m: &[usize]| m.iter().any(|&i| fixed[i]);
            let same_entity = ma
                .iter()
                .any(|&i| mb.iter().any(|&j| keys[i].0.0 == keys[j].0.0));
            if (any_fixed(&ma) && any_fixed(&mb)) || same_entity {
                continue;
            }
            parent[rb] = ra;
        }
        let mut shared = Shared {
            class: HashMap::new(),
            start: HashMap::new(),
        };
        let roots: Vec<usize> = (0..keys.len())
            .map(|i| Self::find(&mut parent, i))
            .collect();
        for (i, (key, at)) in keys.iter().enumerate() {
            if roots.iter().filter(|&&r| r == roots[i]).count() > 1 {
                shared.class.insert(key.clone(), roots[i]);
                if fixed[i] {
                    shared.start.insert(roots[i], *at);
                }
            }
        }
        shared
    }
}

impl Model {
    fn build(sketch: &Sketch) -> Result<Self, Error> {
        let mut model = Model {
            sk: solidcraft_sketch::Sketch::new(),
            slots: HashMap::new(),
        };
        let geometry = |e: solidcraft_sketch::SketchError| Error::Geometry(e.to_string());
        let shared = Shared::new(sketch);
        // The solver point of each merged class, once its first member is created.
        let mut assigned: HashMap<usize, usize> = HashMap::new();
        for (id, entity) in &sketch.entities {
            let class = |role: Option<Role>| shared.class.get(&(id.clone(), role)).copied();
            let existing = |assigned: &HashMap<usize, usize>, role| {
                class(role).and_then(|c| assigned.get(&c).copied())
            };
            // Authored position, or the fixed member's for a merged class.
            let at = |role, p: Point| {
                class(role)
                    .and_then(|c| shared.start.get(&c).copied())
                    .unwrap_or(p)
            };
            if model.slots.contains_key(id) {
                return Err(Error::DuplicateId(id.clone()));
            }
            let created = |m: &Model, curve: usize| m.sk.curves.get(curve).map(|c| c.kind.clone());
            let slot = match entity {
                Entity::Point { at: p } => match existing(&assigned, None) {
                    Some(i) => Slot::Point(i),
                    None => Slot::Point(
                        model
                            .sk
                            .add_point(point(at(None, *p), id)?, None)
                            .map_err(geometry)?,
                    ),
                },
                Entity::Line { start, end } => {
                    if (start[0] - end[0]).hypot(start[1] - end[1]) <= MIN_LENGTH {
                        return Err(invalid(Some(id), format!("{id} has zero length")));
                    }
                    let curve = model
                        .sk
                        .add_line(
                            point(at(Some(Role::Start), *start), id)?,
                            point(at(Some(Role::End), *end), id)?,
                            existing(&assigned, Some(Role::Start)),
                            existing(&assigned, Some(Role::End)),
                            None,
                        )
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
                    let r = positive(*radius, Some(id), || format!("{id} radius"))?;
                    let curve = model
                        .sk
                        .add_circle(
                            point(at(Some(Role::Center), *center), id)?,
                            r,
                            existing(&assigned, Some(Role::Center)),
                            None,
                        )
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
                    let r = positive(*radius, Some(id), || format!("{id} radius"))?;
                    let s = finite(*start_angle, Some(id), || format!("{id} start angle"))?;
                    let w = finite(*sweep_angle, Some(id), || format!("{id} sweep angle"))?;
                    if w == 0.0 || w.abs() >= TAU {
                        return Err(invalid(
                            Some(id),
                            format!("{id} sweep angle (0 < |sweep| < 2π)"),
                        ));
                    }
                    let c = point(at(Some(Role::Center), *center), id)?;
                    let on = |a: f64| Vec2::new(c.x + r * a.cos(), c.y + r * a.sin());
                    let reversed = w < 0.0;
                    // The solver stores arcs counter-clockwise: a clockwise arc runs end to start.
                    let (from_role, to_role) = if reversed {
                        (Role::End, Role::Start)
                    } else {
                        (Role::Start, Role::End)
                    };
                    let (from, to) = if reversed {
                        (on(s + w), on(s))
                    } else {
                        (on(s), on(s + w))
                    };
                    let pts = [
                        existing(&assigned, Some(Role::Center)),
                        existing(&assigned, Some(from_role)),
                        existing(&assigned, Some(to_role)),
                    ];
                    let curve = model.sk.add_arc(c, from, to, pts, None).map_err(geometry)?;
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
            let mut record = |role, p| {
                if let Some(c) = class(role) {
                    assigned.entry(c).or_insert(p);
                }
            };
            match slot {
                Slot::Point(p) => record(None, p),
                Slot::Line { start, end, .. } => {
                    record(Some(Role::Start), start);
                    record(Some(Role::End), end);
                }
                Slot::Circle { center, .. } => record(Some(Role::Center), center),
                Slot::Arc {
                    center, start, end, ..
                } => {
                    record(Some(Role::Center), center);
                    record(Some(Role::Start), start);
                    record(Some(Role::End), end);
                }
            }
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

    /// Start-to-end vector of a line at its current (authored) coordinates.
    fn direction(&self, i: usize, r: &Ref) -> Result<Point, Error> {
        match (self.slot(i, r)?, r.point) {
            (Slot::Line { start, end, .. }, None) => {
                let at = |p: usize| self.sk.point(p).map_or(NAN2, |v| [v.x, v.y]);
                let (s, e) = (at(start), at(end));
                Ok([e[0] - s[0], e[1] - s[1]])
            }
            _ => Self::unsupported(i, r, "expected a line"),
        }
    }

    fn is_point(&self, r: &Ref) -> bool {
        r.point.is_some() || matches!(self.slots.get(&r.entity), Some(Slot::Point(_)))
    }

    fn kinds(&self, i: usize, c: &Constraint) -> Result<Vec<ConstraintKind>, Error> {
        let value = |v: f64| finite(v, None, || format!("constraint {i} value"));
        Ok(match c {
            Constraint::Coincident(a, b) => match (self.is_point(a), self.is_point(b)) {
                (true, true) => {
                    let (p, q) = (self.point_of(i, a)?, self.point_of(i, b)?);
                    // A merged coincidence holds by construction (see `Shared`).
                    if p == q {
                        vec![]
                    } else {
                        vec![ConstraintKind::Coincident { p, q }]
                    }
                }
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
                // RFC 0005 angles are unsigned and independent of operand order; the solver's
                // runs counter-clockwise from `a` to `b`. Keep the authored turning direction.
                let (da, db) = (self.direction(i, a)?, self.direction(i, b)?);
                let v = value(*v)?;
                vec![ConstraintKind::Angle {
                    a: self.line_of(i, a)?,
                    b: self.line_of(i, b)?,
                    value: if da[0] * db[1] - da[1] * db[0] < 0.0 {
                        -v
                    } else {
                        v
                    },
                    flip: false,
                }]
            }
            Constraint::Radius(c, v) => {
                let v = positive(*v, None, || format!("constraint {i} radius"))?;
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

/// Whether a solved entity is geometry the solver accepts as input: finite, lines longer than
/// [`MIN_LENGTH`], radii and arc lengths above it, arcs short of a full turn.
fn valid_geometry(e: &Entity) -> bool {
    let finite = |p: &Point| p.iter().all(|v| v.is_finite());
    match e {
        Entity::Point { at } => finite(at),
        Entity::Line { start, end } => {
            finite(start)
                && finite(end)
                && (start[0] - end[0]).hypot(start[1] - end[1]) > MIN_LENGTH
        }
        Entity::Circle { center, radius } => finite(center) && *radius > MIN_LENGTH,
        Entity::Arc {
            center,
            radius,
            start_angle,
            sweep_angle,
        } => {
            finite(center)
                && *radius > MIN_LENGTH
                && start_angle.is_finite()
                && sweep_angle.is_finite()
                && radius * sweep_angle.abs() > MIN_LENGTH
                && sweep_angle.abs() < TAU
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
    // A failed solve leaves the entities as authored. Merged points (see `Shared`) start at
    // one member's position, so reading them back would move the others.
    let failed = matches!(report.status, SolveStatus::Failed);
    let mut entities: Vec<(String, Entity)> = sketch
        .entities
        .iter()
        .map(|(id, e)| {
            (
                id.clone(),
                if failed { e.clone() } else { model.read(id, e) },
            )
        })
        .collect();
    // Constraints can be met by collapsing a curve (a line's ends made coincident). That is
    // geometry `Model::build` would refuse, so it is a failure with the authored entities.
    let collapsed = entities.iter().any(|(_, e)| !valid_geometry(e));
    if collapsed {
        entities = sketch.entities.clone();
    }
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
                SolveStatus::Solved if !collapsed => Status::Solved,
                _ => Status::Failed,
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
