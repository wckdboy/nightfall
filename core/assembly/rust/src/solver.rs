// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//! Residuals, analytic Jacobians, damped Gauss–Newton and rank diagnostics.
use crate::geometry::{Frame, Placement, Vec3, cross, dot, scale, sub};
use crate::{AssemblyError, MAX_JOINTS, MAX_OCCURRENCES};

/// The relative motion a joint allows between its two frames, measured in frame `a`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum JointKind {
    /// No relative motion.
    Rigid,
    /// Rotation about z. Value: angle.
    Revolute,
    /// Translation along z. Value: length.
    Slider,
    /// Rotation about and translation along z. Values: angle, length.
    Cylindrical,
    /// Rotation about z and translation along x. Values: angle, length.
    PinSlot,
    /// Translation along x and y, rotation about z. Values: length, length, angle.
    Planar,
    /// Three rotations about the shared origin; no driven values.
    Ball,
}

/// One scalar measured between the two frames of a joint.
#[derive(Debug, Clone, Copy, PartialEq)]
enum Measure {
    /// Origin offset of `b` along axis `k` of `a` (mm).
    Offset(usize),
    /// Dot product of axis `u` of `b` with axis `w` of `a` (sine of a small angle, rad).
    Align(usize, usize),
    /// Angle of `b`'s x axis about `a`'s z axis (rad).
    Angle,
}
use Measure::{Align, Angle, Offset};

impl JointKind {
    /// Components that must vanish.
    fn constrained(self) -> &'static [Measure] {
        const TILT: [Measure; 2] = [Align(2, 0), Align(2, 1)];
        match self {
            Self::Rigid => &[
                Offset(0),
                Offset(1),
                Offset(2),
                TILT[0],
                TILT[1],
                Align(0, 1),
            ],
            Self::Revolute => &[Offset(0), Offset(1), Offset(2), TILT[0], TILT[1]],
            Self::Slider => &[Offset(0), Offset(1), TILT[0], TILT[1], Align(0, 1)],
            Self::Cylindrical => &[Offset(0), Offset(1), TILT[0], TILT[1]],
            Self::PinSlot => &[Offset(1), Offset(2), TILT[0], TILT[1]],
            Self::Planar => &[Offset(2), TILT[0], TILT[1]],
            Self::Ball => &[Offset(0), Offset(1), Offset(2)],
        }
    }
    /// Free coordinates that a value may drive, in `values` order.
    fn free(self) -> &'static [Measure] {
        match self {
            Self::Rigid | Self::Ball => &[],
            Self::Revolute => &[Angle],
            Self::Slider => &[Offset(2)],
            Self::Cylindrical => &[Angle, Offset(2)],
            Self::PinSlot => &[Angle, Offset(0)],
            Self::Planar => &[Offset(0), Offset(1), Angle],
        }
    }
    /// Number of entries `values` and `limits` take.
    pub fn value_count(self) -> usize {
        self.free().len()
    }
}

/// A joint between frame `frame_a` on occurrence `a` and `frame_b` on occurrence `b`.
#[derive(Debug, Clone, PartialEq)]
pub struct Joint {
    pub id: String,
    pub kind: JointKind,
    pub a: String,
    pub frame_a: Frame,
    pub b: String,
    pub frame_b: Frame,
    /// One entry per free coordinate ([`JointKind::value_count`]); `Some` drives it.
    pub values: Vec<Option<f64>>,
    /// Empty, or one optional inclusive `(lower, upper)` per free coordinate.
    pub limits: Vec<Option<(f64, f64)>>,
}

impl Joint {
    /// An undriven joint without limits.
    pub fn new(
        id: &str,
        kind: JointKind,
        a: &str,
        frame_a: Frame,
        b: &str,
        frame_b: Frame,
    ) -> Self {
        let (id, a, b) = (id.to_owned(), a.to_owned(), b.to_owned());
        Self {
            id,
            kind,
            a,
            frame_a,
            b,
            frame_b,
            values: vec![None; kind.value_count()],
            limits: vec![],
        }
    }
}

/// A placed instance; grounded occurrences never move.
#[derive(Debug, Clone, PartialEq)]
pub struct Occurrence {
    pub id: String,
    pub grounded: bool,
    pub placement: Placement,
}

#[derive(Debug, Clone, PartialEq, Default)]
pub struct Assembly {
    pub occurrences: Vec<Occurrence>,
    pub joints: Vec<Joint>,
}

/// Acceptance tolerances for a solved assembly and the iteration budget.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct SolveOptions {
    pub length_tolerance: f64,
    pub angle_tolerance: f64,
    pub max_iterations: usize,
}

impl Default for SolveOptions {
    fn default() -> Self {
        Self {
            length_tolerance: 1e-9,
            angle_tolerance: 1e-9,
            max_iterations: 200,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Status {
    /// Every joint holds within tolerance; placements are the solution.
    Solved,
    /// The joints cannot all hold; placements are the unchanged input.
    Failed,
}

/// Largest constrained residual of one joint.
#[derive(Debug, Clone, PartialEq)]
pub struct JointResidual {
    pub joint: String,
    pub length_mm: f64,
    pub angle_rad: f64,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Report {
    pub status: Status,
    /// Remaining mobility: 6 per non-grounded occurrence minus the Jacobian's rank.
    pub dof: usize,
    pub residuals: Vec<JointResidual>,
    /// Satisfied joints whose removal leaves the rank unchanged. The search runs from the last
    /// joint backwards, so a later duplicate is named rather than the joint it repeats.
    pub redundant: Vec<String>,
    /// Joints left unsatisfied at the least-squares point when solving failed.
    pub conflicting: Vec<String>,
    /// Independent cycles of the joint graph; grounded occurrences count as one vertex.
    pub loops: usize,
    pub iterations: usize,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Solution {
    /// One placement per occurrence, in assembly order.
    pub placements: Vec<Placement>,
    pub report: Report,
}

/// Relative tolerance on QR diagonal magnitudes for the rank; explicit by design (RFC 0013).
const RANK_TOLERANCE: f64 = 1e-9;
/// Limit active-set rounds before a violated limit is reported as a conflict.
const LIMIT_ROUNDS: usize = 8;

#[derive(Debug, Clone, Copy)]
struct Row {
    joint: usize,
    measure: Measure,
    target: f64,
}

struct Model<'a> {
    asm: &'a Assembly,
    ends: Vec<(usize, usize)>,
    /// Column offset of each occurrence, or `None` when grounded.
    column: Vec<Option<usize>>,
    columns: usize,
    /// Characteristic length (mm) that scales length tolerances in the rank test.
    length: f64,
}

/// World-space quantities of one joint end.
struct End {
    lever: Vec3,
    origin: Vec3,
    axes: [Vec3; 3],
}

fn end(placement: &Placement, frame: &Frame) -> End {
    let lever = placement.apply_vector(frame.origin);
    let origin = placement.apply_point(frame.origin);
    let axes = [frame.x, frame.y, frame.z].map(|a| placement.apply_vector(a));
    End {
        lever,
        origin,
        axes,
    }
}

fn wrap(angle: f64) -> f64 {
    let t = angle.rem_euclid(std::f64::consts::TAU);
    if t > std::f64::consts::PI {
        t - std::f64::consts::TAU
    } else {
        t
    }
}

impl Model<'_> {
    fn ends(&self, joint: usize, x: &[Placement]) -> (End, End) {
        let j = &self.asm.joints[joint];
        let (a, b) = self.ends[joint];
        (end(&x[a], &j.frame_a), end(&x[b], &j.frame_b))
    }

    fn measure(&self, joint: usize, measure: Measure, x: &[Placement]) -> f64 {
        let (a, b) = self.ends(joint, x);
        match measure {
            Offset(k) => dot(sub(b.origin, a.origin), a.axes[k]),
            Align(u, w) => dot(b.axes[u], a.axes[w]),
            Angle => dot(b.axes[0], a.axes[1]).atan2(dot(b.axes[0], a.axes[0])),
        }
    }

    fn residual(&self, row: &Row, x: &[Placement]) -> f64 {
        let value = self.measure(row.joint, row.measure, x);
        match row.measure {
            Angle => wrap(value - row.target),
            _ => value - row.target,
        }
    }

    /// Gradient of a row as ([ω, v] for end a, [ω, v] for end b) under left perturbations.
    fn gradient(&self, row: &Row, x: &[Placement]) -> ([f64; 6], [f64; 6]) {
        let (a, b) = self.ends(row.joint, x);
        let pack = |w: Vec3, v: Vec3| [w[0], w[1], w[2], v[0], v[1], v[2]];
        let align =
            |ub: Vec3, wa: Vec3| (pack(cross(wa, ub), [0.0; 3]), pack(cross(ub, wa), [0.0; 3]));
        match row.measure {
            Offset(k) => {
                let w = a.axes[k];
                let d = sub(b.origin, a.origin);
                let ga = pack(sub(cross(w, d), cross(a.lever, w)), scale(w, -1.0));
                (ga, pack(cross(b.lever, w), w))
            }
            Align(u, w) => align(b.axes[u], a.axes[w]),
            Angle => {
                let (s, c) = (dot(b.axes[0], a.axes[1]), dot(b.axes[0], a.axes[0]));
                let ((sa, sb), (ca, cb)) =
                    (align(b.axes[0], a.axes[1]), align(b.axes[0], a.axes[0]));
                let n = (s * s + c * c).max(f64::MIN_POSITIVE);
                let combine = |gs: [f64; 6], gc: [f64; 6]| {
                    std::array::from_fn(|i| (c * gs[i] - s * gc[i]) / n)
                };
                (combine(sa, ca), combine(sb, cb))
            }
        }
    }

    fn jacobian(&self, rows: &[Row], x: &[Placement]) -> Vec<Vec<f64>> {
        rows.iter()
            .map(|row| {
                let mut line = vec![0.0; self.columns];
                let (ga, gb) = self.gradient(row, x);
                let (a, b) = self.ends[row.joint];
                for (occurrence, g) in [(a, ga), (b, gb)] {
                    if let Some(c) = self.column[occurrence] {
                        for (i, value) in g.iter().enumerate() {
                            line[c + i] += value;
                        }
                    }
                }
                line
            })
            .collect()
    }

    /// Every joint but a ball keeps its frames' z axes pointing the same way. The tilt rows
    /// only ask z_b to be perpendicular to x_a and y_a, which a flipped part also satisfies.
    /// Likewise `Align(0, 1)` is the sine of the twist about z, zero at a half turn too, so a
    /// rigid joint or a slider also keeps its x axes pointing the same way.
    fn aligned(&self, joint: usize, x: &[Placement]) -> bool {
        let (a, b) = self.ends(joint, x);
        match self.asm.joints[joint].kind {
            JointKind::Ball => true,
            JointKind::Rigid | JointKind::Slider => {
                dot(b.axes[2], a.axes[2]) > 0.0 && dot(b.axes[0], a.axes[0]) > 0.0
            }
            _ => dot(b.axes[2], a.axes[2]) > 0.0,
        }
    }

    fn converged(&self, rows: &[Row], x: &[Placement], opt: &SolveOptions) -> bool {
        rows.iter()
            .all(|row| self.satisfied(row, self.residual(row, x), opt))
            && (0..self.asm.joints.len()).all(|j| self.aligned(j, x))
    }

    fn cost(&self, rows: &[Row], x: &[Placement]) -> f64 {
        rows.iter().map(|row| self.residual(row, x).powi(2)).sum()
    }

    /// The relative placement a joint puts frame b at in frame a: translation by its offsets,
    /// then rotation by its angle, with driven and pinned coordinates at their targets in
    /// `rows` and the others at their values in `start` (a ball keeps its relative rotation).
    fn motion(&self, joint: usize, start: &[Placement], rows: &[Row]) -> Placement {
        let j = &self.asm.joints[joint];
        if j.kind == JointKind::Ball {
            let (a, b) = self.ends(joint, start);
            let r = [0, 1, 2].map(|u| [0, 1, 2].map(|w| dot(a.axes[u], b.axes[w])));
            return Placement {
                rotation: r,
                translation: [0.0; 3],
            };
        }
        let (mut angle, mut t) = (0.0, [0.0; 3]);
        for (k, &measure) in j.kind.free().iter().enumerate() {
            let pinned = rows
                .iter()
                .find(|r| r.joint == joint && r.measure == measure)
                .map(|r| r.target);
            let value = j.values[k]
                .or(pinned)
                .unwrap_or_else(|| self.measure(joint, measure, start));
            match measure {
                Angle => angle = value,
                Offset(i) => t[i] = value,
                Align(..) => {}
            }
        }
        Placement::about_z(angle, t)
    }

    /// Places every occurrence reachable from a grounded one through a spanning tree of the
    /// joints, in joint order, each child exactly through its joint ([`Self::motion`]). A tree
    /// is then solved by construction; a loop starts with only its cut joints open. A connected
    /// part with no grounded occurrence is rooted at its first occurrence, which keeps its
    /// authored placement.
    fn seeded(&self, start: &[Placement], rows: &[Row]) -> Vec<Placement> {
        let mut x = start.to_vec();
        let mut placed: Vec<bool> = self.asm.occurrences.iter().map(|o| o.grounded).collect();
        loop {
            self.grow(start, rows, &mut x, &mut placed);
            match placed.iter().position(|p| !p) {
                Some(root) => placed[root] = true,
                None => return x,
            }
        }
    }

    /// One spanning-tree pass from the `placed` occurrences until nothing more is reachable.
    fn grow(&self, start: &[Placement], rows: &[Row], x: &mut [Placement], placed: &mut [bool]) {
        let mut grew = true;
        while grew {
            grew = false;
            for (joint, j) in self.asm.joints.iter().enumerate() {
                let (a, b) = self.ends[joint];
                if placed[a] == placed[b] {
                    continue;
                }
                let (fa, fb) = (j.frame_a.placement(), j.frame_b.placement());
                let m = self.motion(joint, start, rows);
                if placed[a] {
                    x[b] = x[a].compose(&fa).compose(&m).compose(&fb.inverse());
                    placed[b] = true;
                } else {
                    x[a] = x[b]
                        .compose(&fb)
                        .compose(&m.inverse())
                        .compose(&fa.inverse());
                    placed[a] = true;
                }
                grew = true;
            }
        }
    }

    fn satisfied(&self, row: &Row, residual: f64, opt: &SolveOptions) -> bool {
        let tolerance = if matches!(row.measure, Offset(_)) {
            opt.length_tolerance
        } else {
            opt.angle_tolerance
        };
        residual.abs() <= tolerance
    }

    fn step(&self, x: &[Placement], delta: &[f64]) -> Vec<Placement> {
        x.iter()
            .zip(&self.column)
            .map(|(p, c)| match c {
                Some(c) => p.perturbed(
                    [delta[*c], delta[c + 1], delta[c + 2]],
                    [delta[c + 3], delta[c + 4], delta[c + 5]],
                ),
                None => *p,
            })
            .collect()
    }

    /// Levenberg–Marquardt from `x`; returns the final placements and iterations used.
    fn least_squares(
        &self,
        rows: &[Row],
        mut x: Vec<Placement>,
        opt: &SolveOptions,
    ) -> (Vec<Placement>, usize) {
        let target = |row: &Row| {
            if matches!(row.measure, Offset(_)) {
                1e-13 * self.length
            } else {
                1e-13
            }
        };
        let cost = |r: &[f64]| r.iter().map(|v| v * v).sum::<f64>();
        let mut r: Vec<f64> = rows.iter().map(|row| self.residual(row, &x)).collect();
        let mut lambda = 1e-4;
        let mut iterations = 0;
        while iterations < opt.max_iterations {
            if rows.iter().zip(&r).all(|(row, v)| v.abs() <= target(row)) || self.columns == 0 {
                break;
            }
            iterations += 1;
            let j = self.jacobian(rows, &x);
            let n = self.columns;
            let mut a = vec![vec![0.0; n]; n];
            let mut g = vec![0.0; n];
            for (line, value) in j.iter().zip(&r) {
                for p in 0..n {
                    g[p] += line[p] * value;
                    for q in 0..n {
                        a[p][q] += line[p] * line[q];
                    }
                }
            }
            let peak = (0..n)
                .map(|i| a[i][i])
                .fold(0.0, f64::max)
                .max(f64::MIN_POSITIVE);
            let mut accepted = false;
            while lambda < 1e16 {
                let mut m = a.clone();
                for (i, row) in m.iter_mut().enumerate() {
                    row[i] += lambda * a[i][i].max(1e-9 * peak);
                }
                let Some(delta) = solve_linear(m, g.iter().map(|v| -v).collect()) else {
                    lambda *= 4.0;
                    continue;
                };
                let trial = self.step(&x, &delta);
                let rt: Vec<f64> = rows.iter().map(|row| self.residual(row, &trial)).collect();
                if cost(&rt) < cost(&r) {
                    (x, r) = (trial, rt);
                    lambda = (lambda / 3.0).max(1e-15);
                    accepted = true;
                    break;
                }
                lambda *= 4.0;
            }
            if !accepted {
                break;
            }
        }
        (x, iterations)
    }

    /// The Jacobian with length rows scaled by the model length and columns scaled to unit
    /// norm, so rank decisions compare like with like.
    fn scaled_jacobian(&self, rows: &[Row], x: &[Placement]) -> Vec<Vec<f64>> {
        let mut j = self.jacobian(rows, x);
        for (line, row) in j.iter_mut().zip(rows) {
            if matches!(row.measure, Offset(_)) {
                line.iter_mut().for_each(|v| *v /= self.length);
            }
        }
        for c in 0..self.columns {
            let norm = j.iter().map(|line| line[c] * line[c]).sum::<f64>().sqrt();
            if norm > 0.0 {
                j.iter_mut().for_each(|line| line[c] /= norm);
            }
        }
        j
    }
}

/// Gaussian elimination with partial pivoting; `None` when singular.
fn solve_linear(mut m: Vec<Vec<f64>>, mut b: Vec<f64>) -> Option<Vec<f64>> {
    let n = b.len();
    for k in 0..n {
        let p = (k..n).max_by(|&i, &j| m[i][k].abs().total_cmp(&m[j][k].abs()))?;
        if m[p][k].is_nan() || m[p][k].abs() <= 0.0 {
            return None;
        }
        m.swap(k, p);
        b.swap(k, p);
        let pivot = m[k].clone();
        for i in k + 1..n {
            let f = m[i][k] / pivot[k];
            for (cell, p) in m[i][k..].iter_mut().zip(&pivot[k..]) {
                *cell -= f * p;
            }
            b[i] -= f * b[k];
        }
    }
    let mut x = vec![0.0; n];
    for k in (0..n).rev() {
        let s: f64 = (k + 1..n).map(|c| m[k][c] * x[c]).sum();
        x[k] = (b[k] - s) / m[k][k];
    }
    x.iter().all(|v| v.is_finite()).then_some(x)
}

/// An angle measured in (-pi, pi] moved by whole turns to lie nearest the middle of its
/// limits, so limits that cross the half turn compare correctly; lengths pass through.
fn limited(value: f64, measure: Measure, lo: f64, hi: f64) -> f64 {
    if measure != Angle {
        return value;
    }
    let tau = std::f64::consts::TAU;
    value + tau * ((0.5 * (lo + hi) - value) / tau).round()
}

/// Rank by Householder QR with column pivoting, relative to the largest diagonal.
fn qr_rank(mut a: Vec<Vec<f64>>, cols: usize) -> usize {
    let rows = a.len();
    let mut rank = 0;
    let mut first = 0.0;
    for k in 0..cols.min(rows) {
        let norm2 = |c: usize, a: &[Vec<f64>]| (k..rows).map(|i| a[i][c] * a[i][c]).sum::<f64>();
        let norms: Vec<f64> = (k..cols).map(|c| norm2(c, &a)).collect();
        let Some(p) = (k..cols).max_by(|&i, &j| norms[i - k].total_cmp(&norms[j - k])) else {
            break;
        };
        for line in a.iter_mut() {
            line.swap(k, p);
        }
        let alpha = norm2(k, &a).sqrt();
        if k == 0 {
            first = alpha;
        }
        if first == 0.0 || alpha.is_nan() || alpha <= RANK_TOLERANCE * first {
            break;
        }
        rank += 1;
        let sign = if a[k][k] >= 0.0 { 1.0 } else { -1.0 };
        let mut v: Vec<f64> = (k..rows).map(|i| a[i][k]).collect();
        v[0] += sign * alpha;
        let vv: f64 = v.iter().map(|x| x * x).sum();
        if vv == 0.0 {
            continue;
        }
        let scaled: Vec<f64> = (k..cols)
            .map(|c| (k..rows).map(|i| v[i - k] * a[i][c]).sum::<f64>() * 2.0 / vv)
            .collect();
        for (line, vi) in a.iter_mut().skip(k).zip(&v) {
            for (cell, s) in line[k..cols].iter_mut().zip(&scaled) {
                *cell -= s * vi;
            }
        }
    }
    rank
}

/// Which joints add rank to the joints taken before them, taking joints in `order` and rows by
/// Gram-Schmidt (applied twice for stability) until `rank` independent rows are found: one
/// pass of O(rows * rank * columns) instead of a factorization per joint. `rank` comes from the
/// pivoted QR, so a near-singular pose gives the same rank in any joint order.
fn independent(
    j: &[Vec<f64>],
    rows: &[Row],
    order: impl Iterator<Item = usize>,
    rank: usize,
    joints: usize,
) -> Vec<bool> {
    let mut by_joint: Vec<Vec<usize>> = vec![Vec::new(); joints];
    for (i, row) in rows.iter().enumerate() {
        by_joint[row.joint].push(i);
    }
    let dot = |a: &[f64], b: &[f64]| a.iter().zip(b).map(|(p, q)| p * q).sum::<f64>();
    let mut basis: Vec<Vec<f64>> = Vec::with_capacity(rank);
    let mut adds = vec![false; joints];
    for joint in order {
        for &i in &by_joint[joint] {
            if basis.len() == rank {
                return adds;
            }
            let mut v = j[i].clone();
            for _ in 0..2 {
                for b in &basis {
                    let d = dot(&v, b);
                    v.iter_mut().zip(b).for_each(|(vi, bi)| *vi -= d * bi);
                }
            }
            // Columns have unit norm: the same tolerance as the QR's, relative to its first pivot.
            let norm = dot(&v, &v).sqrt();
            if norm > RANK_TOLERANCE {
                v.iter_mut().for_each(|vi| *vi /= norm);
                basis.push(v);
                adds[joint] = true;
            }
        }
    }
    adds
}

fn validate(asm: &Assembly) -> Result<Vec<(usize, usize)>, AssemblyError> {
    if asm.occurrences.is_empty() {
        return Err(AssemblyError::Empty);
    }
    if asm.occurrences.len() > MAX_OCCURRENCES || asm.joints.len() > MAX_JOINTS {
        return Err(AssemblyError::TooLarge);
    }
    let find = |id: &str| asm.occurrences.iter().position(|o| o.id == id);
    for (index, o) in asm.occurrences.iter().enumerate() {
        if find(&o.id) != Some(index) {
            return Err(AssemblyError::DuplicateOccurrence { index });
        }
        if !o.placement.is_valid() {
            return Err(AssemblyError::InvalidPlacement { occurrence: index });
        }
    }
    let mut ends = Vec::with_capacity(asm.joints.len());
    for (joint, j) in asm.joints.iter().enumerate() {
        if asm.joints.iter().position(|k| k.id == j.id) != Some(joint) {
            return Err(AssemblyError::DuplicateJoint { index: joint });
        }
        if !j.frame_a.is_valid() || !j.frame_b.is_valid() {
            return Err(AssemblyError::DegenerateFrame { joint });
        }
        let (Some(a), Some(b)) = (find(&j.a), find(&j.b)) else {
            return Err(AssemblyError::UnknownOccurrence { joint });
        };
        if a == b {
            return Err(AssemblyError::SameOccurrence { joint });
        }
        let n = j.kind.value_count();
        if j.values.len() != n || !(j.limits.is_empty() || j.limits.len() == n) {
            return Err(AssemblyError::ValueCount { joint });
        }
        for (k, value) in j.values.iter().enumerate() {
            let limit = j.limits.get(k).copied().flatten();
            if value.is_some_and(|v| !v.is_finite())
                || limit.is_some_and(|(lo, hi)| !(lo.is_finite() && hi.is_finite() && lo <= hi))
            {
                return Err(AssemblyError::InvalidValue { joint });
            }
            if let (Some(v), Some((lo, hi))) = (value, limit)
                && (*v < lo || *v > hi)
            {
                return Err(AssemblyError::ValueOutsideLimits { joint });
            }
        }
        ends.push((a, b));
    }
    Ok(ends)
}

fn model(asm: &Assembly) -> Result<Model<'_>, AssemblyError> {
    let ends = validate(asm)?;
    let mut columns = 0;
    let column = asm
        .occurrences
        .iter()
        .map(|o| {
            (!o.grounded).then(|| {
                columns += 6;
                columns - 6
            })
        })
        .collect();
    let norm = |v: Vec3| dot(v, v).sqrt();
    let length = asm
        .occurrences
        .iter()
        .map(|o| norm(o.placement.translation))
        .chain(
            asm.joints
                .iter()
                .flat_map(|j| [norm(j.frame_a.origin), norm(j.frame_b.origin)]),
        )
        .fold(1.0, f64::max);
    Ok(Model {
        asm,
        ends,
        column,
        columns,
        length,
    })
}

/// Free coordinates of every joint at `placements`, in `values` order (mm, rad).
pub fn joint_coordinates(
    asm: &Assembly,
    placements: &[Placement],
) -> Result<Vec<Vec<f64>>, AssemblyError> {
    let m = model(asm)?;
    if placements.len() != asm.occurrences.len() {
        return Err(AssemblyError::InvalidPlacement {
            occurrence: placements.len().min(asm.occurrences.len()),
        });
    }
    if let Some(occurrence) = placements.iter().position(|p| !p.is_valid()) {
        return Err(AssemblyError::InvalidPlacement { occurrence });
    }
    Ok((0..asm.joints.len())
        .map(|j| {
            asm.joints[j]
                .kind
                .free()
                .iter()
                .map(|&f| m.measure(j, f, placements))
                .collect()
        })
        .collect())
}

fn loops(m: &Model) -> usize {
    // Union-find over occurrences, with every grounded occurrence merged into one vertex.
    let n = m.asm.occurrences.len();
    let mut parent: Vec<usize> = (0..n).collect();
    fn root(parent: &mut [usize], mut i: usize) -> usize {
        while parent[i] != i {
            parent[i] = parent[parent[i]];
            i = parent[i];
        }
        i
    }
    let grounded: Vec<usize> = (0..n).filter(|&i| m.asm.occurrences[i].grounded).collect();
    for pair in grounded.windows(2) {
        let (a, b) = (root(&mut parent, pair[0]), root(&mut parent, pair[1]));
        parent[a] = b;
    }
    let vertices = n - grounded.len().saturating_sub(1);
    let mut components = vertices;
    for &(a, b) in &m.ends {
        let (ra, rb) = (root(&mut parent, a), root(&mut parent, b));
        if ra != rb {
            parent[ra] = rb;
            components -= 1;
        }
    }
    (m.ends.len() + components).saturating_sub(vertices)
}

/// Solves every joint from the current placements. Invalid input is an error; joints that cannot
/// all hold give [`Status::Failed`] with the input placements and the conflicting joints named.
pub fn solve(asm: &Assembly, opt: &SolveOptions) -> Result<Solution, AssemblyError> {
    let m = model(asm)?;
    let mut rows: Vec<Row> = Vec::new();
    for (joint, j) in asm.joints.iter().enumerate() {
        rows.extend(j.kind.constrained().iter().map(|&measure| Row {
            joint,
            measure,
            target: 0.0,
        }));
        for (k, &measure) in j.kind.free().iter().enumerate() {
            if let Some(target) = j.values[k] {
                rows.push(Row {
                    joint,
                    measure,
                    target,
                });
            }
        }
    }
    let start: Vec<Placement> = asm.occurrences.iter().map(|o| o.placement).collect();
    // From the authored placements first, which keeps a loop on the branch they describe; if
    // that does not converge (or leaves a part flipped), from the spanning-tree seed, keeping
    // whichever converges, or else the lower cost.
    let (mut x, mut iterations) = m.least_squares(&rows, start.clone(), opt);
    if !m.converged(&rows, &x, opt) {
        let (y, more) = m.least_squares(&rows, m.seeded(&start, &rows), opt);
        iterations += more;
        if m.converged(&rows, &y, opt) || m.cost(&rows, &y) < m.cost(&rows, &x) {
            x = y;
        }
    }
    // Limits: pin a violated undriven coordinate to its nearest bound and solve again.
    for _ in 0..LIMIT_ROUNDS {
        let mut pinned = false;
        for (joint, j) in asm.joints.iter().enumerate() {
            for (k, &measure) in j.kind.free().iter().enumerate() {
                let Some((lo, hi)) = j.limits.get(k).copied().flatten() else {
                    continue;
                };
                if j.values[k].is_some()
                    || rows
                        .iter()
                        .any(|r| r.joint == joint && r.measure == measure)
                {
                    continue;
                }
                let value = limited(m.measure(joint, measure, &x), measure, lo, hi);
                if value < lo || value > hi {
                    rows.push(Row {
                        joint,
                        measure,
                        target: value.clamp(lo, hi),
                    });
                    pinned = true;
                }
            }
        }
        if !pinned {
            break;
        }
        let (next, more) = m.least_squares(&rows, x, opt);
        (x, iterations) = (next, iterations + more);
        // A pin can demand a large move that stalls on a flipped branch, like a far drive: seed
        // again with the pinned coordinates as targets and keep the better result.
        if !m.converged(&rows, &x, opt) {
            let (y, more) = m.least_squares(&rows, m.seeded(&start, &rows), opt);
            iterations += more;
            if m.converged(&rows, &y, opt) || m.cost(&rows, &y) < m.cost(&rows, &x) {
                x = y;
            }
        }
    }
    let residuals: Vec<f64> = rows.iter().map(|row| m.residual(row, &x)).collect();
    // The last limit round can push another undriven coordinate past its bound.
    let mut out_of_limits = vec![false; asm.joints.len()];
    for (joint, j) in asm.joints.iter().enumerate() {
        for (k, &measure) in j.kind.free().iter().enumerate() {
            let Some((lo, hi)) = j.limits.get(k).copied().flatten() else {
                continue;
            };
            let tolerance = if matches!(measure, Offset(_)) {
                opt.length_tolerance
            } else {
                opt.angle_tolerance
            };
            let value = limited(m.measure(joint, measure, &x), measure, lo, hi);
            if j.values[k].is_none() && (value < lo - tolerance || value > hi + tolerance) {
                out_of_limits[joint] = true;
            }
        }
    }
    let solved = rows
        .iter()
        .zip(&residuals)
        .all(|(row, r)| m.satisfied(row, *r, opt))
        && !out_of_limits.contains(&true)
        && (0..asm.joints.len()).all(|j| m.aligned(j, &x));
    let mut report_rows = Vec::with_capacity(asm.joints.len());
    let mut conflicting = Vec::new();
    for (joint, j) in asm.joints.iter().enumerate() {
        let (mut length_mm, mut angle_rad, mut ok) = (0.0_f64, 0.0_f64, true);
        for (row, r) in rows
            .iter()
            .zip(&residuals)
            .filter(|(row, _)| row.joint == joint)
        {
            if matches!(row.measure, Offset(_)) {
                length_mm = length_mm.max(r.abs());
            } else {
                angle_rad = angle_rad.max(r.abs());
            }
            ok &= m.satisfied(row, *r, opt);
        }
        ok &= m.aligned(joint, &x);
        if !ok || out_of_limits[joint] {
            conflicting.push(j.id.clone());
        }
        report_rows.push(JointResidual {
            joint: j.id.clone(),
            length_mm,
            angle_rad,
        });
    }
    // Redundancy: a satisfied joint is redundant when its rows add no rank to the conflicting
    // joints and the joints before it, so a later duplicate is named, not the joint it repeats.
    let conflicts: Vec<bool> = asm
        .joints
        .iter()
        .map(|j| conflicting.contains(&j.id))
        .collect();
    let j = m.scaled_jacobian(&rows, &x);
    let rank = qr_rank(j.clone(), m.columns);
    let joints = asm.joints.len();
    let order = (0..joints)
        .filter(|&k| conflicts[k])
        .chain((0..joints).filter(|&k| !conflicts[k]));
    let adds = independent(&j, &rows, order, rank, joints);
    let redundant: Vec<String> = asm
        .joints
        .iter()
        .zip(adds.iter().zip(&conflicts))
        .filter(|(_, (adds, conflict))| !**adds && !**conflict)
        .map(|(j, _)| j.id.clone())
        .collect();
    let status = if solved {
        Status::Solved
    } else {
        Status::Failed
    };
    let report = Report {
        status,
        dof: m.columns - rank.min(m.columns),
        residuals: report_rows,
        redundant,
        conflicting: if solved { vec![] } else { conflicting },
        loops: loops(&m),
        iterations,
    };
    Ok(Solution {
        placements: if solved { x } else { start },
        report,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_small_early_row_does_not_raise_the_rank_of_a_near_singular_pose() {
        // Two unit columns; joint 0's row is tiny and nearly parallel to joint 1's. The second
        // QR pivot is about 2e-15, so the rank is 1 in either joint order, and only the first
        // joint taken adds rank.
        let (s, t) = (1e-6_f64, 1e-6_f64 + 2e-15);
        let j = vec![vec![s, t], vec![(1.0 - s * s).sqrt(), (1.0 - t * t).sqrt()]];
        let rows: Vec<Row> = (0..2)
            .map(|joint| Row {
                joint,
                measure: Angle,
                target: 0.0,
            })
            .collect();
        let rank = qr_rank(j.clone(), 2);
        assert_eq!(rank, 1);
        assert_eq!(
            independent(&j, &rows, [0, 1].into_iter(), rank, 2),
            [true, false]
        );
        assert_eq!(
            independent(&j, &rows, [1, 0].into_iter(), rank, 2),
            [false, true]
        );
    }
}
