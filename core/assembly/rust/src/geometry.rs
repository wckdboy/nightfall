// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//! Rigid placements and joint frames in millimetres.
use crate::AssemblyError;

pub type Vec3 = [f64; 3];
type Mat3 = [[f64; 3]; 3];

/// Orthonormality and handedness tolerance for authored rotations and frames.
const UNIT_TOLERANCE: f64 = 1e-9;

pub(crate) fn add(a: Vec3, b: Vec3) -> Vec3 {
    [a[0] + b[0], a[1] + b[1], a[2] + b[2]]
}
pub(crate) fn sub(a: Vec3, b: Vec3) -> Vec3 {
    [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
}
pub(crate) fn scale(a: Vec3, s: f64) -> Vec3 {
    [a[0] * s, a[1] * s, a[2] * s]
}
pub(crate) fn dot(a: Vec3, b: Vec3) -> f64 {
    a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
}
pub(crate) fn cross(a: Vec3, b: Vec3) -> Vec3 {
    [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    ]
}
fn finite(a: Vec3) -> bool {
    a.iter().all(|x| x.is_finite())
}
fn normalized(a: Vec3) -> Option<Vec3> {
    let n = dot(a, a).sqrt();
    (n.is_finite() && n > 1e-12).then(|| scale(a, 1.0 / n))
}
fn mat_vec(m: &Mat3, v: Vec3) -> Vec3 {
    [dot(m[0], v), dot(m[1], v), dot(m[2], v)]
}
fn mat_mul(a: &Mat3, b: &Mat3) -> Mat3 {
    let mut out = [[0.0; 3]; 3];
    for (i, row) in out.iter_mut().enumerate() {
        for (j, cell) in row.iter_mut().enumerate() {
            *cell = (0..3).map(|k| a[i][k] * b[k][j]).sum();
        }
    }
    out
}
fn orthonormal(axes: [Vec3; 3]) -> bool {
    let [x, y, z] = axes;
    axes.iter()
        .all(|a| finite(*a) && (dot(*a, *a) - 1.0).abs() <= UNIT_TOLERANCE)
        && dot(x, y).abs() <= UNIT_TOLERANCE
        && dot(y, z).abs() <= UNIT_TOLERANCE
        && dot(z, x).abs() <= UNIT_TOLERANCE
        && dot(cross(x, y), z) > 0.0
}

/// Rotation matrix of the rotation vector `w` (Rodrigues), exact for every angle.
fn exp_so3(w: Vec3) -> Mat3 {
    let theta2 = dot(w, w);
    let theta = theta2.sqrt();
    // sin(t)/t and (1 - cos(t))/t^2 with series near zero.
    let (a, b) = if theta < 1e-6 {
        (1.0 - theta2 / 6.0, 0.5 - theta2 / 24.0)
    } else {
        (theta.sin() / theta, (1.0 - theta.cos()) / theta2)
    };
    let k = [[0.0, -w[2], w[1]], [w[2], 0.0, -w[0]], [-w[1], w[0], 0.0]];
    let k2 = mat_mul(&k, &k);
    let mut r = [[0.0; 3]; 3];
    for i in 0..3 {
        for j in 0..3 {
            r[i][j] = f64::from(u8::from(i == j)) + a * k[i][j] + b * k2[i][j];
        }
    }
    r
}

/// A rigid placement: world point = rotation · local point + translation (mm).
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Placement {
    /// Row-major proper rotation matrix.
    pub rotation: [[f64; 3]; 3],
    pub translation: Vec3,
}

impl Placement {
    pub const IDENTITY: Self = Self {
        rotation: [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]],
        translation: [0.0; 3],
    };

    /// Rotation by `angle` radians about the unit direction of `axis`, then translation.
    pub fn from_axis_angle(axis: Vec3, angle: f64, translation: Vec3) -> Option<Self> {
        let unit = normalized(axis)?;
        (angle.is_finite() && finite(translation)).then(|| Self {
            rotation: exp_so3(scale(unit, angle)),
            translation,
        })
    }
    pub fn apply_point(&self, p: Vec3) -> Vec3 {
        add(mat_vec(&self.rotation, p), self.translation)
    }
    pub fn apply_vector(&self, v: Vec3) -> Vec3 {
        mat_vec(&self.rotation, v)
    }
    pub(crate) fn is_valid(&self) -> bool {
        let r = self.rotation;
        finite(self.translation) && orthonormal([r[0], r[1], r[2]])
    }
    /// Left perturbation: rotation by `w` about the world origin's axes, translation by `v`.
    pub(crate) fn perturbed(&self, w: Vec3, v: Vec3) -> Self {
        let r = mat_mul(&exp_so3(w), &self.rotation);
        // Re-orthonormalize the rows so repeated steps do not drift.
        let x = normalized(r[0]).unwrap_or([1.0, 0.0, 0.0]);
        let z = normalized(cross(x, r[1])).unwrap_or([0.0, 0.0, 1.0]);
        Self {
            rotation: [x, cross(z, x), z],
            translation: add(self.translation, v),
        }
    }
}

/// A right-handed orthonormal frame in an occurrence's local coordinates (mm).
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Frame {
    pub origin: Vec3,
    pub x: Vec3,
    pub y: Vec3,
    pub z: Vec3,
}

impl Frame {
    /// Frame with the unit direction of `z` and `x` the part of `x_hint` perpendicular to it.
    pub fn new(origin: Vec3, z: Vec3, x_hint: Vec3) -> Result<Self, AssemblyError> {
        let degenerate = AssemblyError::DegenerateFrame { joint: usize::MAX };
        let z = normalized(z).ok_or(degenerate)?;
        let x = normalized(sub(x_hint, scale(z, dot(x_hint, z)))).ok_or(degenerate)?;
        if !finite(origin) {
            return Err(degenerate);
        }
        Ok(Self {
            origin,
            x,
            y: cross(z, x),
            z,
        })
    }
    /// World-aligned axes at `origin`.
    pub fn at(origin: Vec3) -> Self {
        Self {
            origin,
            x: [1.0, 0.0, 0.0],
            y: [0.0, 1.0, 0.0],
            z: [0.0, 0.0, 1.0],
        }
    }
    pub(crate) fn is_valid(&self) -> bool {
        finite(self.origin) && orthonormal([self.x, self.y, self.z])
    }
}
