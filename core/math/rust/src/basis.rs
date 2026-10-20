// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use crate::{KnotVector, MAX_DEGREE, MathError, SIDE};

/// Nonzero N_{span-p+j,p}(u) in entries j=0..=p; unused entries are zero.
#[derive(Debug, PartialEq)]
pub struct BasisValues {
    pub span: usize,
    pub values: [f64; SIDE],
}

/// `values[k][j]` is derivative k of N_{span-p+j,p}(u); unused entries are zero.
#[derive(Debug, PartialEq)]
pub struct BasisDerivatives {
    pub span: usize,
    pub values: [[f64; SIDE]; SIDE],
}

fn parameter(knots: &KnotVector, u: f64) -> Result<(usize, f64), MathError> {
    let span = knots.find_span(u)?;
    let (a, b) = knots.domain();
    if !(b - a).is_finite() {
        return Err(MathError::NumericalRange);
    }
    Ok((span, u.clamp(a, b)))
}

/// A2.2 (The NURBS Book), using fixed stack workspaces and no allocation.
/// Finite u clamps to the domain; unrepresentable arithmetic returns `NumericalRange`.
pub fn basis_functions(knots: &KnotVector, u: f64) -> Result<BasisValues, MathError> {
    let (span, u) = parameter(knots, u)?;
    let mut out = BasisValues {
        span,
        values: [0.0; SIDE],
    };
    let (mut left, mut right) = ([0.0; SIDE], [0.0; SIDE]);
    out.values[0] = 1.0;
    for j in 1..=knots.degree() {
        left[j] = u - knots.knots()[span + 1 - j];
        right[j] = knots.knots()[span + j] - u;
        let mut saved = 0.0;
        for r in 0..j {
            let temp = out.values[r] / (right[r + 1] + left[j - r]);
            out.values[r] = saved + right[r + 1] * temp;
            saved = left[j - r] * temp;
        }
        out.values[j] = saved;
    }
    if out.values.iter().any(|v| !v.is_finite()) {
        return Err(MathError::NumericalRange);
    }
    Ok(out)
}

/// A2.3, allocation-free derivatives through order 9; orders above the degree are zero.
/// Repeated knots use right-hand derivatives (left-hand at the domain end).
#[allow(clippy::needless_range_loop)] // A2.3's triangular recurrences depend on array indices.
pub fn basis_derivatives(
    knots: &KnotVector,
    u: f64,
    order: usize,
) -> Result<BasisDerivatives, MathError> {
    if order > MAX_DEGREE {
        return Err(MathError::DerivativeOrder);
    }
    let (span, u) = parameter(knots, u)?;
    let p = knots.degree();
    let orders = order.min(p);
    let mut out = BasisDerivatives {
        span,
        values: [[0.0; SIDE]; SIDE],
    };
    let mut ndu = [[0.0; SIDE]; SIDE];
    let (mut left, mut right) = ([0.0; SIDE], [0.0; SIDE]);
    ndu[0][0] = 1.0;
    for j in 1..=p {
        left[j] = u - knots.knots()[span + 1 - j];
        right[j] = knots.knots()[span + j] - u;
        let mut saved = 0.0;
        for r in 0..j {
            ndu[j][r] = right[r + 1] + left[j - r];
            let temp = ndu[r][j - 1] / ndu[j][r];
            ndu[r][j] = saved + right[r + 1] * temp;
            saved = left[j - r] * temp;
        }
        ndu[j][j] = saved;
    }
    for j in 0..=p {
        out.values[0][j] = ndu[j][p];
    }
    let mut a = [[0.0; SIDE]; 2];
    for r in 0..=p {
        let (mut s1, mut s2) = (0, 1);
        a[0][0] = 1.0;
        for k in 1..=orders {
            let pk = p - k;
            let mut d = 0.0;
            if r >= k {
                a[s2][0] = a[s1][0] / ndu[pk + 1][r - k];
                d = a[s2][0] * ndu[r - k][pk];
            }
            let j1 = if r + 1 >= k { 1 } else { k - r };
            let j2 = if r <= pk + 1 { k - 1 } else { p - r };
            for j in j1..=j2 {
                let index = r + j - k;
                a[s2][j] = (a[s1][j] - a[s1][j - 1]) / ndu[pk + 1][index];
                d += a[s2][j] * ndu[index][pk];
            }
            if r <= pk {
                a[s2][k] = -a[s1][k - 1] / ndu[pk + 1][r];
                d += a[s2][k] * ndu[r][pk];
            }
            out.values[k][r] = d;
            std::mem::swap(&mut s1, &mut s2);
        }
    }
    let mut factor = p as f64;
    for k in 1..=orders {
        for j in 0..=p {
            out.values[k][j] *= factor;
        }
        factor *= (p - k) as f64;
    }
    if out.values.iter().flatten().any(|v| !v.is_finite()) {
        return Err(MathError::NumericalRange);
    }
    Ok(out)
}
