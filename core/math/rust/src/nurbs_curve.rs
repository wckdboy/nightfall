// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use crate::{KnotVector, MathError, SIDE, basis_derivatives, basis_functions};
use std::sync::Arc;

/// Checked immutable 2D/3D NURBS geometry, independent of any native kernel.
///
/// ```
/// use nightfall_math::{KnotVector, MathError, NurbsCurve3};
/// # fn main() -> Result<(), MathError> {
/// let knots = KnotVector::new(2, vec![0., 0., 0., 1., 1., 1.])?;
/// let arc = NurbsCurve3::new(knots,
///     vec![[1., 0., 0.], [1., 1., 0.], [0., 1., 0.]],
///     vec![1., 2_f64.sqrt() / 2., 1.])?;
/// let point = arc.point(0.5)?;
/// assert!((point[0] - std::f64::consts::FRAC_1_SQRT_2).abs() <= 1e-15);
/// let derivatives = arc.derivatives::<3>(0.5)?;
/// assert!((point[0]*derivatives[1][0] + point[1]*derivatives[1][1]).abs() <= 1e-13);
/// # Ok(())
/// # }
/// ```
#[derive(Debug, Clone)]
pub struct NurbsCurve<const D: usize> {
    data: Arc<CurveData<D>>,
}
#[derive(Debug)]
struct CurveData<const D: usize> {
    knots: KnotVector,
    points: Vec<[f64; D]>,
    weights: Vec<f64>,
    rational: bool,
}
/// Two-dimensional NURBS curve.
pub type NurbsCurve2 = NurbsCurve<2>;
/// Three-dimensional NURBS curve.
pub type NurbsCurve3 = NurbsCurve<3>;

pub(crate) fn normalize(weights: &mut [f64]) -> Result<bool, MathError> {
    if weights.iter().any(|w| !w.is_finite() || *w <= 0.0) {
        return Err(MathError::InvalidWeight);
    }
    if weights.iter().all(|w| *w == weights[0]) {
        weights.fill(1.0);
        return Ok(false);
    }
    let mut largest = weights.iter().copied().fold(0.0, f64::max);
    // First lift subnormals exactly; the remaining scale factor is representable.
    if largest < f64::MIN_POSITIVE {
        let scale = 1.0 / f64::MIN_POSITIVE;
        for w in weights.iter_mut() {
            *w *= scale;
        }
        largest *= scale;
    }
    let exponent = ((largest.to_bits() >> 52) & 0x7ff) as i32 - 1023;
    let scale = if exponent == 1023 {
        f64::MIN_POSITIVE / 2.0
    } else {
        f64::from_bits(((1023 - exponent) as u64) << 52)
    };
    for w in weights.iter_mut() {
        *w *= scale;
    }
    if weights.iter().any(|w| !w.is_normal()) {
        return Err(MathError::WeightRange);
    }
    Ok(true)
}

impl<const D: usize> NurbsCurve<D> {
    /// Finite controls matching the knots, D=2 or 3, and empty or positive finite weights.
    /// Equal weights become ones; varying weights scale by an exact power of two so the
    /// largest is in [1,2). Reject ranges whose normalized weights are not all normal.
    pub fn new(
        knots: KnotVector,
        points: Vec<[f64; D]>,
        mut weights: Vec<f64>,
    ) -> Result<Self, MathError> {
        if D != 2 && D != 3 {
            return Err(MathError::Dimension);
        }
        if points.len() != knots.control_point_count() {
            return Err(MathError::ControlPointCount);
        }
        if points.iter().flatten().any(|v| !v.is_finite()) {
            return Err(MathError::NonFinitePoint);
        }
        if weights.is_empty() {
            weights.resize(points.len(), 1.0);
        }
        if weights.len() != points.len() {
            return Err(MathError::WeightCount);
        }
        let rational = normalize(&mut weights)?;
        Ok(Self {
            data: Arc::new(CurveData {
                knots,
                points,
                weights,
                rational,
            }),
        })
    }
    /// Borrowed checked knots; ownership is retained by every curve clone.
    pub fn knots(&self) -> &KnotVector {
        &self.data.knots
    }
    /// Borrowed immutable control coordinates.
    pub fn points(&self) -> &[[f64; D]] {
        &self.data.points
    }
    /// Normalized weights, one per control point (all ones for polynomial curves).
    pub fn weights(&self) -> &[f64] {
        &self.data.weights
    }
    /// Whether the weights vary.
    pub fn rational(&self) -> bool {
        self.data.rational
    }

    /// A3.1/A4.1 point evaluation, without allocation. Finite u clamps to the domain;
    /// nonfinite u and unrepresentable arithmetic return structured errors.
    pub fn point(&self, u: f64) -> Result<[f64; D], MathError> {
        let mut b = basis_functions(self.knots(), u)?;
        let p = self.knots().degree();
        if self.rational() {
            let mut weight = 0.0;
            for j in 0..=p {
                b.values[j] *= self.weights()[b.span - p + j];
                weight += b.values[j];
            }
            // Normalize the blend before applying coordinates: tiny homogeneous
            // products can underflow even when their quotient is representable.
            for value in &mut b.values[..=p] {
                *value /= weight;
            }
        }
        let mut point = [0.0; D];
        for j in 0..=p {
            for (value, coordinate) in point.iter_mut().zip(self.points()[b.span - p + j]) {
                *value += b.values[j] * coordinate;
            }
        }
        if point.iter().any(|v| !v.is_finite()) {
            return Err(MathError::NumericalRange);
        }
        Ok(point)
    }

    /// A3.2/A4.2 returns C(u), C'(u), … through order N-1 in an owned stack array.
    /// Rational derivatives above the degree remain supported; polynomial ones are zero.
    /// Basis workspaces are degree-bounded; the caller chooses the output array size.
    #[allow(clippy::needless_range_loop)] // Indexed rational quotient recurrence.
    pub fn derivatives<const N: usize>(&self, u: f64) -> Result<[[f64; D]; N], MathError> {
        if N < 2 {
            let mut out = [[0.0; D]; N];
            if N == 0 {
                self.knots().find_span(u)?;
            } else {
                out[0] = self.point(u)?;
            }
            return Ok(out);
        }
        let orders = N.saturating_sub(1).min(self.knots().degree());
        let b = basis_derivatives(self.knots(), u, orders)?;
        // Differentiate relative coordinates so translations and constant controls do
        // not contaminate high derivatives through cancellation of large coordinates.
        let origin = self.points()[b.span - self.knots().degree()];
        let weight = if self.rational() {
            (0..=self.knots().degree())
                .map(|j| b.values[0][j] * self.weights()[b.span - self.knots().degree() + j])
                .sum()
        } else {
            1.0
        };
        let mut aders = [[0.0; D]; SIDE];
        let mut wders = [0.0; SIDE];
        for k in 0..=orders {
            for j in 0..=self.knots().degree() {
                let i = b.span - self.knots().degree() + j;
                // Normalize before multiplying relative coordinates, as in point().
                let coefficient = (b.values[k][j] * self.weights()[i]) / weight;
                for axis in 0..D {
                    aders[k][axis] += coefficient * (self.points()[i][axis] - origin[axis]);
                }
                wders[k] += coefficient;
            }
        }
        let mut out = [[0.0; D]; N];
        for k in 0..N {
            let mut v = if k <= orders { aders[k] } else { [0.0; D] };
            if self.rational() {
                let mut binomial = 1.0;
                for i in 1..=k.min(orders) {
                    binomial = binomial * (k - i + 1) as f64 / i as f64;
                    for axis in 0..D {
                        v[axis] -= out[k - i][axis] * (binomial * wders[i]);
                    }
                }
            }
            out[k] = v;
        }
        if let Some(point) = out.first_mut() {
            for axis in 0..D {
                point[axis] += origin[axis];
            }
        }
        if out.iter().flatten().any(|v| !v.is_finite()) {
            return Err(MathError::NumericalRange);
        }
        Ok(out)
    }
}
