// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use nightfall_math::{KnotVector, MathError, NurbsCurve, NurbsCurve2, NurbsCurve3};

fn knots(degree: usize) -> KnotVector {
    let mut values = vec![0.0; degree + 1];
    values.extend(std::iter::repeat_n(1.0, degree + 1));
    KnotVector::new(degree, values).unwrap()
}
fn near<const D: usize>(a: [f64; D], b: [f64; D], tolerance: f64) {
    for (x, y) in a.into_iter().zip(b) {
        assert!(
            (x - y).abs() <= tolerance,
            "{x} != {y}, tolerance {tolerance}"
        );
    }
}
fn arc() -> NurbsCurve3 {
    NurbsCurve3::new(
        knots(2),
        vec![[1., 0., 0.], [1., 1., 0.], [0., 1., 0.]],
        vec![1., 2_f64.sqrt() / 2., 1.],
    )
    .unwrap()
}

#[test]
fn checked_curve_construction() {
    assert_eq!(
        NurbsCurve::<4>::new(knots(1), vec![[0.; 4]; 2], vec![]).unwrap_err(),
        MathError::Dimension
    );
    assert_eq!(
        NurbsCurve2::new(knots(1), vec![[0.; 2]], vec![]).unwrap_err(),
        MathError::ControlPointCount
    );
    assert_eq!(
        NurbsCurve2::new(knots(1), vec![[0.; 2]; 2], vec![1.]).unwrap_err(),
        MathError::WeightCount
    );
    for bad in [f64::NAN, f64::INFINITY, f64::NEG_INFINITY] {
        assert_eq!(
            NurbsCurve2::new(knots(1), vec![[bad, 0.], [1., 0.]], vec![]).unwrap_err(),
            MathError::NonFinitePoint
        );
    }
    for bad in [0., -0., -1., f64::NAN, f64::INFINITY, f64::NEG_INFINITY] {
        assert_eq!(
            NurbsCurve2::new(knots(1), vec![[0.; 2]; 2], vec![1., bad]).unwrap_err(),
            MathError::InvalidWeight
        );
    }
    assert_eq!(
        NurbsCurve2::new(knots(1), vec![[0.; 2]; 2], vec![f64::MAX, 1e-300]).unwrap_err(),
        MathError::WeightRange
    );
}

#[test]
fn nonrational_lines_and_all_derivatives_are_analytic() {
    let c = NurbsCurve2::new(knots(1), vec![[1., 2.], [5., 10.]], vec![]).unwrap();
    assert!(!c.rational());
    assert_eq!(c.weights(), &[1., 1.]);
    near(c.point(0.25).unwrap(), [2., 4.], 1e-15);
    let d = c.derivatives::<13>(0.25).unwrap();
    assert_eq!(d[0], [2., 4.]);
    assert_eq!(d[1], [4., 8.]);
    assert_eq!(d[2..], [[0.; 2]; 11]);
    assert!(c.derivatives::<0>(0.25).unwrap().is_empty());
}

#[test]
fn rational_conic_points_tangents_and_curvature_are_analytic() {
    let c = arc();
    assert!(c.rational());
    for i in 0..=256 {
        let d = c.derivatives::<3>(i as f64 / 256.).unwrap();
        near(c.point(i as f64 / 256.).unwrap(), d[0], 1e-14);
        let norm = d[0].iter().map(|v| v * v).sum::<f64>().sqrt();
        assert!((norm - 1.).abs() <= 1e-15);
        let tangent = (0..3).map(|j| d[0][j] * d[1][j]).sum::<f64>();
        assert!(tangent.abs() <= 1e-13);
        let second = (0..3)
            .map(|j| d[0][j] * d[2][j] + d[1][j] * d[1][j])
            .sum::<f64>();
        assert!(second.abs() <= 1e-12);
    }
}

#[test]
fn rational_derivatives_above_degree_do_not_vanish() {
    let c = NurbsCurve2::new(knots(1), vec![[0., 0.], [1., 0.]], vec![1., 2.]).unwrap();
    let d = c.derivatives::<13>(0.5).unwrap();
    near(d[0], [2. / 3., 0.], 1e-15);
    let mut factorial = 1.0;
    for (k, value) in d.iter().enumerate().skip(1) {
        factorial *= k as f64;
        let exact =
            -2. * if k % 2 == 0 { 1. } else { -1. } * factorial / 1.5_f64.powi(k as i32 + 1);
        near(*value, [exact, 0.], 1e-10 * exact.abs());
    }
}

#[test]
fn weight_normalization_is_exact_and_handles_subnormals() {
    let make = |w| NurbsCurve2::new(knots(1), vec![[0., 0.], [1., 3.]], w).unwrap();
    let a = make(vec![1., 3.]);
    let b = make(vec![4., 12.]);
    assert_eq!(a.weights(), b.weights());
    for u in [0., 0.1, 0.5, 0.77, 1.] {
        assert_eq!(a.point(u), b.point(u));
        assert_eq!(a.derivatives::<13>(u), b.derivatives::<13>(u));
    }
    let huge = make(vec![f64::MAX, f64::MAX / 2.]);
    assert!(huge.weights()[0] >= 1. && huge.weights()[0] < 2.);
    assert!(huge.point(0.5).unwrap().iter().all(|v| v.is_finite()));
    let equal = make(vec![f64::MAX; 2]);
    assert!(!equal.rational());
    assert_eq!(equal.weights(), &[1., 1.]);
    let tiny = make(vec![f64::from_bits(1), f64::from_bits(2)]);
    assert_eq!(tiny.weights(), &[0.5, 1.]);
    near(tiny.point(0.5).unwrap(), [2. / 3., 2.], 1e-15);
}

#[test]
fn endpoints_clamp_and_nonfinite_parameters_fail() {
    let c = arc();
    assert_eq!(c.point(-f64::MAX), c.point(0.));
    assert_eq!(c.point(f64::MAX), c.point(1.));
    near(c.point(0.).unwrap(), [1., 0., 0.], 1e-15);
    near(c.point(1.).unwrap(), [0., 1., 0.], 1e-15);
    assert_eq!(c.derivatives::<13>(-20.), c.derivatives::<13>(0.));
    for u in [f64::NAN, f64::INFINITY, f64::NEG_INFINITY] {
        assert_eq!(c.point(u), Err(MathError::NonFiniteParameter));
        assert_eq!(c.derivatives::<13>(u), Err(MathError::NonFiniteParameter));
        assert_eq!(c.derivatives::<0>(u), Err(MathError::NonFiniteParameter));
    }
}

#[test]
fn unrepresentable_curve_derivatives_return_an_error() {
    let c = NurbsCurve2::new(knots(1), vec![[-f64::MAX, 0.], [f64::MAX, 0.]], vec![]).unwrap();
    assert_eq!(c.point(0.5), Ok([0., 0.]));
    assert_eq!(c.derivatives::<1>(0.5), Ok([[0., 0.]]));
    assert_eq!(c.derivatives::<2>(0.5), Err(MathError::NumericalRange));
}

#[test]
fn constant_coordinates_cannot_create_high_order_derivatives() {
    for degree in 1..=9 {
        for rational in [false, true] {
            let k = knots(degree);
            let points = vec![[1e12, -7., 42.]; k.control_point_count()];
            let weights = if rational {
                (0..points.len()).map(|i| (i % 5 + 1) as f64).collect()
            } else {
                vec![]
            };
            let c = NurbsCurve3::new(k, points, weights).unwrap();
            for u in [0., 0.1, 0.37, 0.5, 0.9, 1.] {
                let d = c.derivatives::<13>(u).unwrap();
                assert_eq!(d[0], [1e12, -7., 42.]);
                assert_eq!(d[1..], [[0.; 3]; 12]);
            }
        }
    }
}

#[test]
fn large_translations_preserve_rational_derivatives() {
    let original = NurbsCurve2::new(knots(1), vec![[0., 0.], [1., 0.]], vec![1., 2.]).unwrap();
    let moved =
        NurbsCurve2::new(knots(1), vec![[1e12, 3.], [1e12 + 1., 3.]], vec![1., 2.]).unwrap();
    for u in [0., 0.1, 0.5, 0.9, 1.] {
        let a = original.derivatives::<13>(u).unwrap();
        let b = moved.derivatives::<13>(u).unwrap();
        for k in 1..13 {
            near(a[k], b[k], 1e-10 * a[k][0].abs());
        }
    }
}

#[test]
fn small_homogeneous_products_preserve_representable_geometry() {
    let tiny = 1e-300;
    let c = NurbsCurve2::new(knots(1), vec![[tiny, 0.], [2. * tiny, 0.]], vec![tiny, 1.]).unwrap();
    assert_eq!(c.point(0.), Ok([tiny, 0.]));
    let point = c.point(tiny).unwrap();
    assert!((point[0] / tiny - 1.5).abs() <= 1e-14);
    let d = c.derivatives::<3>(tiny).unwrap();
    assert!((d[0][0] / tiny - 1.5).abs() <= 1e-14);
    assert!((d[1][0] - 0.25).abs() <= 1e-14);
    assert!((d[2][0] * tiny + 0.25).abs() <= 1e-14);
    let start = c.derivatives::<3>(0.).unwrap();
    assert!((start[1][0] - 1.).abs() <= 1e-14);
    assert!((start[2][0] * tiny + 2.).abs() <= 1e-14);
    assert_eq!(c.derivatives::<4>(0.), Err(MathError::NumericalRange));
}

#[test]
fn bernstein_polynomials_have_analytic_derivatives_at_every_order() {
    // Alternating Bernstein control values give (1-2u)^p exactly.
    for p in 1..=9 {
        let points = (0..=p)
            .map(|i| [0., if i % 2 == 0 { 1. } else { -1. }])
            .collect();
        let c = NurbsCurve2::new(knots(p), points, vec![]).unwrap();
        for u in [0., 0.25, 0.5, 0.75, 1.] {
            let d = c.derivatives::<13>(u).unwrap();
            let mut factor = 1.0;
            for (k, value) in d.iter().enumerate().take(p + 1) {
                let exact = factor * (1. - 2. * u).powi((p - k) as i32);
                near(*value, [0., exact], 1e-13 * (1. + exact.abs()));
                factor *= -2. * (p - k) as f64;
            }
        }
    }
}

#[test]
fn retained_curve_storage_is_shareable_without_a_kernel() {
    fn send_sync<T: Send + Sync>() {}
    send_sync::<NurbsCurve2>();
    send_sync::<NurbsCurve3>();
    let c = arc();
    let retained = c.clone();
    drop(c);
    assert_eq!(
        retained.points().len(),
        retained.knots().control_point_count()
    );
    let expected = retained.derivatives::<13>(0.37).unwrap();
    std::thread::scope(|scope| {
        for _ in 0..8 {
            let reader = &retained;
            scope.spawn(move || assert_eq!(reader.derivatives::<13>(0.37).unwrap(), expected));
        }
    });
}

#[test]
fn all_degrees_preserve_affine_geometry_and_finite_difference_derivatives() {
    let transform = |p: [f64; 3]| [2. * p[1] + 3., -2. * p[0] - 4., 2. * p[2] + 1.];
    for degree in 1..=9 {
        let mut values = vec![0.; degree + 1];
        values.push(0.3);
        values.extend(std::iter::repeat_n(0.5, degree));
        values.push(0.85);
        values.extend(std::iter::repeat_n(1., degree + 1));
        let k = KnotVector::new(degree, values).unwrap();
        let points: Vec<_> = (0..k.control_point_count())
            .map(|i| [i as f64 / 3., (i % 5) as f64 - 2., (i % 3) as f64])
            .collect();
        for rational in [false, true] {
            let weights: Vec<_> = if rational {
                (0..points.len())
                    .map(|i| 0.25 + (i % 5) as f64 * 0.375)
                    .collect()
            } else {
                vec![]
            };
            let c = NurbsCurve3::new(k.clone(), points.clone(), weights.clone()).unwrap();
            let moved = NurbsCurve3::new(
                k.clone(),
                points.iter().copied().map(transform).collect(),
                weights,
            )
            .unwrap();
            for sample in 0..45 {
                let u = if sample < 5 {
                    [0., 0.3, 0.5, 0.85, 1.][sample]
                } else {
                    (sample as f64 - 4.5) / 40.
                };
                let d = c.derivatives::<13>(u).unwrap();
                assert_eq!(d, c.derivatives::<13>(u).unwrap());
                near(
                    moved.point(u).unwrap(),
                    transform(c.point(u).unwrap()),
                    1e-12,
                );
                if !rational {
                    assert!(d[degree + 1..].iter().flatten().all(|v| *v == 0.));
                }
                const H: f64 = 1e-6;
                if u > H && u < 1. - H && k.find_span(u - H) == k.find_span(u + H) {
                    let a = c.derivatives::<2>(u - H).unwrap();
                    let b = c.derivatives::<2>(u + H).unwrap();
                    for j in 0..3 {
                        let first = (b[0][j] - a[0][j]) / (2. * H);
                        let second = (b[1][j] - a[1][j]) / (2. * H);
                        assert!((d[1][j] - first).abs() <= 1e-6 * (1. + first.abs()));
                        assert!((d[2][j] - second).abs() <= 1e-4 * (1. + second.abs()));
                    }
                }
            }
        }
    }
}
