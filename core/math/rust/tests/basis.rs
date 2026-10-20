// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use nightfall_math::{KnotVector, MAX_DEGREE, MathError, basis_derivatives, basis_functions};

fn near(actual: f64, expected: f64, tolerance: f64) {
    assert!(
        (actual - expected).abs() <= tolerance,
        "{actual} != {expected}, tolerance {tolerance}"
    );
}
fn knots(degree: usize, values: &[f64]) -> KnotVector {
    KnotVector::new(degree, values.to_vec()).unwrap()
}
fn mixed(degree: usize) -> KnotVector {
    let mut values = vec![0.0; degree + 1];
    values.push(0.2);
    values.extend(std::iter::repeat_n(0.5, degree));
    values.push(0.8);
    values.extend(std::iter::repeat_n(1.0, degree + 1));
    KnotVector::new(degree, values).unwrap()
}

#[test]
fn checked_knot_construction() {
    for degree in [0, MAX_DEGREE + 1, usize::MAX] {
        assert_eq!(
            KnotVector::new(degree, vec![]).unwrap_err(),
            MathError::Degree
        );
    }
    assert_eq!(
        KnotVector::new(2, vec![0.0, 0.0, 1.0, 1.0]).unwrap_err(),
        MathError::KnotCount
    );
    for bad in [f64::NAN, f64::INFINITY, f64::NEG_INFINITY] {
        assert_eq!(
            KnotVector::new(1, vec![0.0, 0.0, bad, 1.0, 1.0]).unwrap_err(),
            MathError::NonFiniteKnot
        );
    }
    assert_eq!(
        KnotVector::new(1, vec![0.0, 0.0, 0.7, 0.3, 1.0, 1.0]).unwrap_err(),
        MathError::KnotOrder
    );
    for values in [
        vec![0.0, 0.1, 1.0, 1.0],
        vec![0.0, 0.0, 0.9, 1.0],
        vec![0.0; 4],
    ] {
        assert!(KnotVector::new(1, values).is_err());
    }
    assert_eq!(
        KnotVector::new(2, vec![0.0, 0.0, 0.0, 0.5, 0.5, 0.5, 1.0, 1.0, 1.0]).unwrap_err(),
        MathError::InteriorMultiplicity
    );
}

#[test]
fn immutable_knots_and_span_endpoints() {
    let k = knots(2, &[2.0, 2.0, 2.0, 3.0, 5.0, 5.0, 5.0]);
    let retained = k.clone();
    assert_eq!(k.degree(), 2);
    assert_eq!(k.control_point_count(), 4);
    assert_eq!(k.domain(), (2.0, 5.0));
    for (u, span) in [
        (-100.0, 2),
        (2.0, 2),
        (2.9, 2),
        (3.0, 3),
        (5.0, 3),
        (100.0, 3),
    ] {
        assert_eq!(k.find_span(u), Ok(span));
    }
    drop(k);
    assert_eq!(retained.knots(), &[2.0, 2.0, 2.0, 3.0, 5.0, 5.0, 5.0]);
}

#[test]
fn linear_basis_and_derivatives_are_analytic() {
    let k = knots(1, &[0.0, 0.0, 1.0, 1.0]);
    let b = basis_functions(&k, 0.25).unwrap();
    assert_eq!(b.span, 1);
    assert_eq!(&b.values[..2], &[0.75, 0.25]);
    assert_eq!(&b.values[2..], &[0.0; 8]);
    let d = basis_derivatives(&k, 0.25, MAX_DEGREE).unwrap();
    assert_eq!(&d.values[0][..2], &[0.75, 0.25]);
    assert_eq!(&d.values[1][..2], &[-1.0, 1.0]);
    assert!(d.values[2..].iter().flatten().all(|v| *v == 0.0));
}

#[test]
fn quadratic_bernstein_derivatives_are_analytic() {
    let k = knots(2, &[0.0, 0.0, 0.0, 1.0, 1.0, 1.0]);
    let d = basis_derivatives(&k, 0.25, 2).unwrap();
    for (row, expected) in [[0.5625, 0.375, 0.0625], [-1.5, 1.0, 0.5], [2.0, -4.0, 2.0]]
        .iter()
        .enumerate()
    {
        for (j, value) in expected.iter().enumerate() {
            near(d.values[row][j], *value, 1e-15);
        }
    }
}

#[test]
fn repeated_knots_choose_the_right_hand_span() {
    let k = knots(3, &[0.0, 0.0, 0.0, 0.0, 0.5, 0.5, 0.5, 1.0, 1.0, 1.0, 1.0]);
    let d = basis_derivatives(&k, 0.5, 3).unwrap();
    assert_eq!(d.span, 6);
    for (row, expected) in [
        [1.0, 0.0, 0.0, 0.0],
        [-6.0, 6.0, 0.0, 0.0],
        [24.0, -48.0, 24.0, 0.0],
        [-48.0, 144.0, -144.0, 48.0],
    ]
    .iter()
    .enumerate()
    {
        for (j, value) in expected.iter().enumerate() {
            near(d.values[row][j], *value, 1e-12);
        }
    }
}

#[test]
fn finite_outside_parameters_clamp_to_the_domain() {
    for degree in 1..=MAX_DEGREE {
        let k = mixed(degree);
        assert_eq!(basis_functions(&k, -7.0), basis_functions(&k, 0.0));
        assert_eq!(
            basis_derivatives(&k, 12.0, degree),
            basis_derivatives(&k, 1.0, degree)
        );
    }
}

#[test]
fn nonfinite_parameters_and_unsupported_orders_fail() {
    let k = mixed(3);
    for u in [f64::NAN, f64::INFINITY, f64::NEG_INFINITY] {
        assert_eq!(k.find_span(u), Err(MathError::NonFiniteParameter));
        assert_eq!(basis_functions(&k, u), Err(MathError::NonFiniteParameter));
        assert_eq!(
            basis_derivatives(&k, u, 1),
            Err(MathError::NonFiniteParameter)
        );
    }
    assert_eq!(
        basis_derivatives(&k, 0.5, MAX_DEGREE + 1),
        Err(MathError::DerivativeOrder)
    );
}

#[test]
fn unrepresentable_numerics_return_errors() {
    let k = knots(1, &[-f64::MAX, -f64::MAX, f64::MAX, f64::MAX]);
    assert_eq!(basis_functions(&k, 0.0), Err(MathError::NumericalRange));
    let k = knots(1, &[0.0, 0.0, 1e-310, 1e-310]);
    assert_eq!(
        basis_derivatives(&k, 0.0, 1),
        Err(MathError::NumericalRange)
    );
}

#[test]
fn partition_derivatives_and_determinism_cover_all_degrees() {
    let mut state = 20_261_020_u64;
    for degree in 1..=MAX_DEGREE {
        let k = mixed(degree);
        for sample in 0..160 {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            let u = if sample < 5 {
                [0.0, 0.2, 0.5, 0.8, 1.0][sample]
            } else {
                (state >> 11) as f64 / ((1_u64 << 53) as f64)
            };
            let b = basis_functions(&k, u).unwrap();
            let d = basis_derivatives(&k, u, MAX_DEGREE).unwrap();
            assert_eq!(d, basis_derivatives(&k, u, MAX_DEGREE).unwrap());
            near(b.values.iter().sum(), 1.0, 2e-14);
            assert!(b.values.iter().all(|v| *v >= 0.0));
            for j in 0..=degree {
                near(b.values[j], d.values[0][j], 1e-15);
            }
            for row in 1..=MAX_DEGREE {
                let scale: f64 = d.values[row].iter().map(|v| v.abs()).sum();
                near(d.values[row].iter().sum(), 0.0, 1e-12 * (1.0 + scale));
            }
            const H: f64 = 1e-6;
            if u > H && u < 1.0 - H && k.find_span(u - H) == k.find_span(u + H) {
                let a = basis_functions(&k, u - H).unwrap();
                let z = basis_functions(&k, u + H).unwrap();
                for j in 0..=degree {
                    let finite_difference = (z.values[j] - a.values[j]) / (2.0 * H);
                    near(
                        d.values[1][j],
                        finite_difference,
                        1e-7 * (1.0 + finite_difference.abs()),
                    );
                }
            }
        }
    }
}
