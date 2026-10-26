// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use crate::{KnotVector, MathError, SIDE, basis_derivatives, basis_functions};
use std::sync::Arc;

/// Immutable checked tensor-product NURBS surface, independent of a native kernel.
/// Controls have `count_u` rows of `count_v` points: P(i,j) = points[i*count_v+j].
/// Degrees, knots, positive weight normalization and dimensions follow `NurbsCurve`.
///
/// ```
/// use nightfall_math::{KnotVector, MathError, NurbsSurface3};
/// # fn main() -> Result<(), MathError> {
/// let knots = KnotVector::new(1, vec![0., 0., 1., 1.])?;
/// let patch = NurbsSurface3::new(knots.clone(), knots,
///     vec![[0.,0.,0.], [0.,1.,0.], [1.,0.,0.], [1.,1.,1.]], vec![])?;
/// let partials = patch.derivatives::<2>(0.25, 0.5)?;
/// assert_eq!(partials[1][1], [0., 0., 1.]);
/// # Ok(())
/// # }
/// ```
#[derive(Debug, Clone)]
pub struct NurbsSurface<const D: usize> {
    data: Arc<SurfaceData<D>>,
}
#[derive(Debug)]
struct SurfaceData<const D: usize> {
    ku: KnotVector,
    kv: KnotVector,
    points: Vec<[f64; D]>,
    weights: Vec<f64>,
    rational: bool,
}
/// Two-dimensional NURBS surface.
pub type NurbsSurface2 = NurbsSurface<2>;
/// Three-dimensional NURBS surface.
pub type NurbsSurface3 = NurbsSurface<3>;

fn product(a: f64, b: f64) -> Result<f64, MathError> {
    let value = a * b;
    if !value.is_finite() || (a != 0. && b != 0. && (value == 0. || value.is_subnormal())) {
        return Err(MathError::NumericalRange);
    }
    Ok(value)
}
fn check_basis(
    knots: &KnotVector,
    span: usize,
    u: f64,
    values: &[f64; SIDE],
) -> Result<(), MathError> {
    // All active functions are positive strictly inside a nonempty knot span.
    // A zero/subnormal there may lose bits that rational normalization amplifies.
    if u > knots.knots()[span]
        && u < knots.knots()[span + 1]
        && values[..=knots.degree()]
            .iter()
            .any(|v| *v == 0. || v.is_subnormal())
    {
        return Err(MathError::NumericalRange);
    }
    Ok(())
}

impl<const D: usize> NurbsSurface<D> {
    /// D=2 or 3, finite row-major controls, empty or one positive finite weight per pole.
    /// Checked knot vectors determine the grid size; a mismatched/overflowing count fails.
    /// Equal weights become ones; other weights use the curve's exact power-of-two scale.
    pub fn new(
        ku: KnotVector,
        kv: KnotVector,
        points: Vec<[f64; D]>,
        mut weights: Vec<f64>,
    ) -> Result<Self, MathError> {
        if D != 2 && D != 3 {
            return Err(MathError::Dimension);
        }
        if ku
            .control_point_count()
            .checked_mul(kv.control_point_count())
            != Some(points.len())
        {
            return Err(MathError::ControlPointCount);
        }
        if points.iter().flatten().any(|v| !v.is_finite()) {
            return Err(MathError::NonFinitePoint);
        }
        if weights.is_empty() {
            weights.resize(points.len(), 1.);
        }
        if weights.len() != points.len() {
            return Err(MathError::WeightCount);
        }
        let rational = crate::nurbs_curve::normalize(&mut weights)?;
        Ok(Self {
            data: Arc::new(SurfaceData {
                ku,
                kv,
                points,
                weights,
                rational,
            }),
        })
    }
    /// Borrowed immutable u knots.
    pub fn knots_u(&self) -> &KnotVector {
        &self.data.ku
    }
    /// Borrowed immutable v knots.
    pub fn knots_v(&self) -> &KnotVector {
        &self.data.kv
    }
    /// Number of rows in the control net.
    pub fn count_u(&self) -> usize {
        self.knots_u().control_point_count()
    }
    /// Number of columns in the control net; each row varies in v.
    pub fn count_v(&self) -> usize {
        self.knots_v().control_point_count()
    }
    /// Borrowed immutable row-major coordinates, retained by every surface clone.
    pub fn points(&self) -> &[[f64; D]] {
        &self.data.points
    }
    /// Normalized weights, one per control (ones for polynomial surfaces).
    pub fn weights(&self) -> &[f64] {
        &self.data.weights
    }
    /// Whether the weights vary.
    pub fn rational(&self) -> bool {
        self.data.rational
    }
    fn index(&self, su: usize, sv: usize, i: usize, j: usize) -> usize {
        (su - self.knots_u().degree() + i) * self.count_v() + sv - self.knots_v().degree() + j
    }
    fn patch_scale(&self, su: usize, sv: usize) -> f64 {
        let mut largest: f64 = 0.;
        for i in 0..=self.knots_u().degree() {
            for j in 0..=self.knots_v().degree() {
                largest = largest.max(self.weights()[self.index(su, sv, i, j)]);
            }
        }
        largest
    }

    /// Allocation-free A4.3 point evaluation. Finite parameters clamp independently;
    /// nonfinite parameters and unrepresentable arithmetic return structured errors.
    /// Subnormal basis/blend/product intermediates conservatively return `NumericalRange`
    /// before rational normalization can amplify their lost precision.
    pub fn point(&self, u: f64, v: f64) -> Result<[f64; D], MathError> {
        let bu = basis_functions(self.knots_u(), u)?;
        let bv = basis_functions(self.knots_v(), v)?;
        check_basis(self.knots_u(), bu.span, u, &bu.values)?;
        check_basis(self.knots_v(), bv.span, v, &bv.values)?;
        let mut blend = [[0.; SIDE]; SIDE];
        let scale = self.patch_scale(bu.span, bv.span);
        let mut denominator = 0.;
        for (i, row) in blend
            .iter_mut()
            .enumerate()
            .take(self.knots_u().degree() + 1)
        {
            for (j, value) in row.iter_mut().enumerate().take(self.knots_v().degree() + 1) {
                *value = product(
                    product(bu.values[i], bv.values[j])?,
                    self.weights()[self.index(bu.span, bv.span, i, j)] / scale,
                )?;
                denominator += *value;
            }
        }
        // Polynomial partition of unity is exact mathematically. Renormalizing
        // its rounded sum would perturb both the value and its derivative scale.
        if !self.rational() {
            denominator = 1.;
        }
        let mut out = [0.; D];
        for (i, row) in blend.iter().enumerate().take(self.knots_u().degree() + 1) {
            for (j, value) in row.iter().enumerate().take(self.knots_v().degree() + 1) {
                let coefficient = value / denominator;
                for (result, coordinate) in out
                    .iter_mut()
                    .zip(self.points()[self.index(bu.span, bv.span, i, j)])
                {
                    *result += product(coefficient, coordinate)?;
                }
            }
        }
        if out.iter().any(|x| !x.is_finite()) {
            return Err(MathError::NumericalRange);
        }
        Ok(out)
    }

    /// Returns S_{u^k v^l} in [k][l], 0<=k,l<N, in a caller-sized stack array.
    /// Includes all rectangular mixed orders, not only k+l<N. Rational orders above
    /// the degrees are supported; above-degree polynomial partials are exactly zero.
    /// Repeated knots use right-hand derivatives (left-hand at the domain end).
    /// No evaluation/clone allocates. Intermediate overflow returns `NumericalRange`,
    /// even when the point alone is representable. No normal or error bound is inferred.
    /// Subnormal basis/product intermediates use the same conservative guard as `point`.
    #[allow(clippy::needless_range_loop)] // Tensor derivative and quotient recurrences.
    pub fn derivatives<const N: usize>(
        &self,
        u: f64,
        v: f64,
    ) -> Result<[[[f64; D]; N]; N], MathError> {
        let mut out = [[[0.; D]; N]; N];
        if N < 2 {
            if N == 0 {
                self.knots_u().find_span(u)?;
                self.knots_v().find_span(v)?;
            } else {
                out[0][0] = self.point(u, v)?;
            }
            return Ok(out);
        }
        let p = self.knots_u().degree();
        let q = self.knots_v().degree();
        let ou = (N - 1).min(p);
        let ov = (N - 1).min(q);
        let bu = basis_derivatives(self.knots_u(), u, ou)?;
        let bv = basis_derivatives(self.knots_v(), v, ov)?;
        check_basis(self.knots_u(), bu.span, u, &bu.values[0])?;
        check_basis(self.knots_v(), bv.span, v, &bv.values[0])?;
        let origin = self.points()[self.index(bu.span, bv.span, 0, 0)];
        let scale = self.patch_scale(bu.span, bv.span);
        let mut denominator = 1.;
        if self.rational() {
            denominator = 0.;
            for i in 0..=p {
                for j in 0..=q {
                    denominator += product(
                        product(bu.values[0][i], bv.values[0][j])?,
                        self.weights()[self.index(bu.span, bv.span, i, j)] / scale,
                    )?;
                }
            }
        }
        let mut ad = [[[0.; D]; SIDE]; SIDE];
        let mut wd = [[0.; SIDE]; SIDE];
        for k in 0..=ou {
            for l in 0..=ov {
                for i in 0..=p {
                    for j in 0..=q {
                        let index = self.index(bu.span, bv.span, i, j);
                        let coefficient = product(
                            product(bu.values[k][i], bv.values[l][j])?,
                            self.weights()[index] / scale,
                        )? / denominator;
                        for axis in 0..D {
                            ad[k][l][axis] +=
                                product(coefficient, self.points()[index][axis] - origin[axis])?;
                        }
                        wd[k][l] += coefficient;
                    }
                }
            }
        }
        for k in 0..N {
            for l in 0..N {
                let mut value = if k <= ou && l <= ov {
                    ad[k][l]
                } else {
                    [0.; D]
                };
                if self.rational() {
                    let mut choose_u = 1.;
                    for i in 0..=k.min(ou) {
                        if i > 0 {
                            choose_u = choose_u * (k - i + 1) as f64 / i as f64;
                        }
                        let mut choose_v = 1.;
                        for j in 0..=l.min(ov) {
                            if j > 0 {
                                choose_v = choose_v * (l - j + 1) as f64 / j as f64;
                            }
                            if i + j == 0 {
                                continue;
                            }
                            for axis in 0..D {
                                value[axis] -=
                                    choose_u * choose_v * wd[i][j] * out[k - i][l - j][axis];
                            }
                        }
                    }
                }
                out[k][l] = value;
            }
        }
        for axis in 0..D {
            out[0][0][axis] += origin[axis];
        }
        if out.iter().flatten().flatten().any(|v| !v.is_finite()) {
            return Err(MathError::NumericalRange);
        }
        Ok(out)
    }
}
