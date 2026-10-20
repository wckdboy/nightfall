// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use crate::{MAX_DEGREE, MathError};
use std::sync::Arc;

/// Immutable, cheaply cloned clamped knots; all invariants are checked by `new`.
#[derive(Debug, Clone)]
pub struct KnotVector {
    degree: usize,
    knots: Arc<[f64]>,
}

impl KnotVector {
    /// Degree 1..=9, finite nondecreasing knots, end multiplicity p+1, interior <=p.
    pub fn new(degree: usize, knots: Vec<f64>) -> Result<Self, MathError> {
        if !(1..=MAX_DEGREE).contains(&degree) {
            return Err(MathError::Degree);
        }
        if knots.len() < 2 * (degree + 1) {
            return Err(MathError::KnotCount);
        }
        if knots.iter().any(|v| !v.is_finite()) {
            return Err(MathError::NonFiniteKnot);
        }
        if knots.windows(2).any(|w| w[0] > w[1]) {
            return Err(MathError::KnotOrder);
        }
        let end = knots.len() - degree - 1;
        if knots[degree] >= knots[end] {
            return Err(MathError::EmptyDomain);
        }
        if knots[..=degree].iter().any(|v| *v != knots[0])
            || knots[end..].iter().any(|v| *v != knots[end])
            || knots[degree + 1] == knots[0]
            || knots[end - 1] == knots[end]
        {
            return Err(MathError::EndMultiplicity);
        }
        let mut start = degree + 1;
        while start < end {
            let count = knots[start..end]
                .iter()
                .take_while(|v| **v == knots[start])
                .count();
            if count > degree {
                return Err(MathError::InteriorMultiplicity);
            }
            start += count;
        }
        Ok(Self {
            degree,
            knots: knots.into(),
        })
    }

    /// Polynomial degree, checked to be in 1..=9.
    pub fn degree(&self) -> usize {
        self.degree
    }
    /// Borrowed immutable knot storage, including repeated knots.
    pub fn knots(&self) -> &[f64] {
        &self.knots
    }
    /// Required number of control points for this knot vector.
    pub fn control_point_count(&self) -> usize {
        self.knots.len() - self.degree - 1
    }
    /// Nonempty closed parameter interval (start, end).
    pub fn domain(&self) -> (f64, f64) {
        (
            self.knots[self.degree],
            self.knots[self.control_point_count()],
        )
    }

    /// Right-hand span at repeated knots; finite outside parameters clamp to the domain.
    /// The domain end belongs to the last nonempty span. Nonfinite inputs are rejected.
    pub fn find_span(&self, u: f64) -> Result<usize, MathError> {
        if !u.is_finite() {
            return Err(MathError::NonFiniteParameter);
        }
        let (a, b) = self.domain();
        if u <= a {
            return Ok(self.degree);
        }
        let mut low = self.degree;
        let mut high = self.control_point_count();
        if u >= b {
            return Ok(high - 1);
        }
        while high - low > 1 {
            let mid = low + (high - low) / 2;
            if u < self.knots[mid] {
                high = mid;
            } else {
                low = mid;
            }
        }
        Ok(low)
    }
}
