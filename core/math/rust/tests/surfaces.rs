// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use nightfall_math::{KnotVector, MathError, NurbsSurface, NurbsSurface2, NurbsSurface3};

fn knots(degree: usize) -> KnotVector {
    let mut values = vec![0.; degree + 1];
    values.extend(vec![1.; degree + 1]);
    KnotVector::new(degree, values).unwrap()
}
fn near(actual: f64, expected: f64, tolerance: f64) {
    assert!(
        actual.is_finite() && (actual - expected).abs() <= tolerance,
        "{actual:.17e} != {expected:.17e}, tolerance {tolerance:.3e}"
    );
}
fn plane(translation: f64) -> NurbsSurface3 {
    NurbsSurface3::new(
        knots(1),
        knots(1),
        vec![
            [translation, 0., 2.],
            [translation, 3., 1.],
            [translation + 4., 0., 4.],
            [translation + 4., 3., 3.],
        ],
        vec![],
    )
    .unwrap()
}

#[test]
fn planes_have_exact_mixed_derivatives_and_polynomial_zeros() {
    for translation in [0., 1e12] {
        let surface = plane(translation);
        assert_eq!((surface.count_u(), surface.count_v()), (2, 2));
        assert!(!surface.rational());
        for (u, v) in [(0., 0.), (1., 1.), (0.25, 0.75), (-2., 3.)] {
            let d = surface.derivatives::<13>(u, v).unwrap();
            let (u, v) = (u.clamp(0., 1.), v.clamp(0., 1.));
            let expected = [translation + 4. * u, 3. * v, 2. + 2. * u - v];
            for axis in 0..3 {
                near(d[0][0][axis], expected[axis], 1e-13);
                near(surface.point(u, v).unwrap()[axis], expected[axis], 1e-13);
                near(d[1][0][axis], [4., 0., 2.][axis], 1e-13);
                near(d[0][1][axis], [0., 3., -1.][axis], 1e-13);
            }
            for (k, row) in d.iter().enumerate() {
                for (l, point) in row.iter().enumerate() {
                    if k > 1 || l > 1 || (k == 1 && l == 1) {
                        assert_eq!(*point, [0.; 3]);
                    }
                }
            }
        }
    }
}

#[test]
fn bilinear_patch_and_non_unit_parameters() {
    let ku = KnotVector::new(1, vec![2., 2., 6., 6.]).unwrap();
    let kv = KnotVector::new(1, vec![-3., -3., 2., 2.]).unwrap();
    let s =
        NurbsSurface2::new(ku, kv, vec![[0., 0.], [0., 0.], [0., 0.], [1., 2.]], vec![]).unwrap();
    let d = s.derivatives::<4>(3., -1.).unwrap();
    assert_eq!(d[0][0], [0.1, 0.2]);
    assert_eq!(d[1][1], [0.05, 0.1]);
    assert!(s.derivatives::<0>(3., -1.).unwrap().is_empty());
    assert_eq!(
        s.derivatives::<1>(3., -1.).unwrap()[0][0],
        s.point(3., -1.).unwrap()
    );
}

#[test]
fn rational_cylinder_has_unit_radius_and_tangent_derivatives() {
    let w = std::f64::consts::FRAC_1_SQRT_2;
    let surface = NurbsSurface3::new(
        knots(2),
        knots(1),
        vec![
            [1., 0., 0.],
            [1., 0., 5.],
            [1., 1., 0.],
            [1., 1., 5.],
            [0., 1., 0.],
            [0., 1., 5.],
        ],
        vec![1., 1., w, w, 1., 1.],
    )
    .unwrap();
    for i in 0..=32 {
        let u = i as f64 / 32.;
        let d = surface.derivatives::<5>(u, 0.375).unwrap();
        let p = d[0][0];
        near(p[0] * p[0] + p[1] * p[1], 1., 8e-15);
        near(p[2], 1.875, 2e-15);
        near(p[0] * d[1][0][0] + p[1] * d[1][0][1], 0., 5e-14);
        for axis in 0..3 {
            near(d[0][1][axis], [0., 0., 5.][axis], 2e-14);
        }
        for row in d.iter().skip(1) {
            for point in row.iter().skip(1) {
                for value in point {
                    near(*value, 0., 1e-10);
                }
            }
        }
    }
}

#[test]
fn rational_mixed_derivatives_above_degree_match_closed_form() {
    // S(u,v) = (1/((1+u)(1+2v)), u/(1+u), v/(1+2v)).
    let s = NurbsSurface3::new(
        knots(1),
        knots(1),
        vec![
            [1., 0., 0.],
            [1. / 3., 0., 1. / 3.],
            [0.5, 0.5, 0.],
            [1. / 6., 0.5, 1. / 3.],
        ],
        vec![1., 3., 2., 6.],
    )
    .unwrap();
    let (u, v) = (0.25_f64, 0.375_f64);
    let d = s.derivatives::<7>(u, v).unwrap();
    let factorial = |n: usize| (1..=n).fold(1., |v, i| v * i as f64);
    for (k, row) in d.iter().enumerate() {
        for (l, point) in row.iter().enumerate() {
            let expected =
                (-1_f64).powi((k + l) as i32) * factorial(k) * factorial(l) * 2_f64.powi(l as i32)
                    / ((1. + u).powi((k + 1) as i32) * (1. + 2. * v).powi((l + 1) as i32));
            near(point[0], expected, 5e-12 * expected.abs().max(1.));
            if k > 0 && l > 0 {
                near(point[1], 0., 5e-12 * expected.abs().max(1.));
                near(point[2], 0., 5e-12 * expected.abs().max(1.));
            }
        }
    }
}

#[test]
fn repeated_knots_use_right_hand_derivatives_and_left_at_end() {
    let ku = KnotVector::new(1, vec![0., 0., 0.5, 1., 1.]).unwrap();
    let s = NurbsSurface2::new(
        ku,
        knots(1),
        vec![[0., 0.], [0., 1.], [1., 0.], [1., 1.], [4., 0.], [4., 1.]],
        vec![],
    )
    .unwrap();
    near(s.derivatives::<2>(0.25, 0.4).unwrap()[1][0][0], 2., 1e-14);
    for u in [0.5, 1., 2.] {
        near(s.derivatives::<2>(u, 0.4).unwrap()[1][0][0], 6., 1e-14);
    }
}

#[test]
fn mixed_partials_match_independent_central_differences() {
    let s = NurbsSurface3::new(
        knots(3),
        knots(2),
        (0..12)
            .map(|i| [(i % 4) as f64, (i * i % 7) as f64, (i * i * i % 11) as f64])
            .collect(),
        (0..12).map(|i| (i % 5 + 1) as f64).collect(),
    )
    .unwrap();
    let h = 2e-5;
    for (u, v) in [(0.2, 0.3), (0.47, 0.61), (0.8, 0.7)] {
        let d = s.derivatives::<3>(u, v).unwrap();
        let up = s.derivatives::<3>(u + h, v).unwrap();
        let um = s.derivatives::<3>(u - h, v).unwrap();
        let vp = s.derivatives::<3>(u, v + h).unwrap();
        let vm = s.derivatives::<3>(u, v - h).unwrap();
        for axis in 0..3 {
            for k in 0..2 {
                for l in 0..2 {
                    near(
                        d[k + 1][l][axis],
                        (up[k][l][axis] - um[k][l][axis]) / (2. * h),
                        2e-5,
                    );
                    near(
                        d[k][l + 1][axis],
                        (vp[k][l][axis] - vm[k][l][axis]) / (2. * h),
                        2e-5,
                    );
                }
            }
        }
    }
}

#[test]
fn construction_rejects_invalid_nets_and_evaluation_rejects_nonfinite_parameters() {
    let make = |p, w| NurbsSurface3::new(knots(1), knots(1), p, w);
    assert_eq!(
        NurbsSurface::<1>::new(knots(1), knots(1), vec![[0.]; 4], vec![]).unwrap_err(),
        MathError::Dimension
    );
    assert_eq!(
        make(vec![[0.; 3]; 3], vec![]).unwrap_err(),
        MathError::ControlPointCount
    );
    assert_eq!(
        make(vec![[f64::NAN; 3]; 4], vec![]).unwrap_err(),
        MathError::NonFinitePoint
    );
    assert_eq!(
        make(vec![[0.; 3]; 4], vec![1.; 3]).unwrap_err(),
        MathError::WeightCount
    );
    for weight in [0., -1., f64::NAN, f64::INFINITY] {
        assert_eq!(
            make(vec![[0.; 3]; 4], vec![1., 1., 1., weight]).unwrap_err(),
            MathError::InvalidWeight
        );
    }
    assert_eq!(
        make(vec![[0.; 3]; 4], vec![f64::MIN_POSITIVE, f64::MAX, 1., 1.]).unwrap_err(),
        MathError::WeightRange
    );
    let s = plane(0.);
    for value in [f64::NAN, f64::INFINITY, f64::NEG_INFINITY] {
        for (u, v) in [(value, 0.5), (0.5, value)] {
            assert_eq!(s.point(u, v), Err(MathError::NonFiniteParameter));
            assert_eq!(s.derivatives::<0>(u, v), Err(MathError::NonFiniteParameter));
            assert_eq!(s.derivatives::<3>(u, v), Err(MathError::NonFiniteParameter));
        }
    }
}

#[test]
fn retained_surfaces_share_immutable_storage_across_threads() {
    let s = plane(0.);
    let retained = s.clone();
    assert!(std::ptr::eq(
        s.points().as_ptr(),
        retained.points().as_ptr()
    ));
    drop(s);
    assert_eq!(
        std::thread::spawn(move || retained.point(0.25, 0.75).unwrap())
            .join()
            .unwrap(),
        [1., 2.25, 1.75]
    );
}

#[test]
fn rational_sphere_includes_collapsed_polar_edges() {
    let w = std::f64::consts::FRAC_1_SQRT_2;
    let arc = [[1., 0.], [1., 1.], [0., 1.]];
    let mut points = vec![];
    let mut weights = vec![];
    for i in 0..3 {
        for j in 0..3 {
            points.push([arc[i][0] * arc[j][0], arc[i][0] * arc[j][1], arc[i][1]]);
            weights.push([1., w, 1.][i] * [1., w, 1.][j]);
        }
    }
    let s = NurbsSurface3::new(knots(2), knots(2), points, weights).unwrap();
    for i in 0..=8 {
        for j in 0..=8 {
            let d = s.derivatives::<2>(i as f64 / 8., j as f64 / 8.).unwrap();
            near(d[0][0].iter().map(|v| v * v).sum(), 1., 1e-14);
            for tangent in [d[1][0], d[0][1]] {
                near((0..3).map(|a| d[0][0][a] * tangent[a]).sum(), 0., 1e-13);
            }
        }
    }
}

#[test]
fn uniform_weight_scale_and_large_translations_preserve_derivatives() {
    let points = vec![[0., 2., 4.], [2., -1., 3.], [4., 1., 2.], [3., 2., 1.]];
    let weights = vec![1., 3., 2., 5.];
    let base = NurbsSurface3::new(knots(1), knots(1), points.clone(), weights.clone()).unwrap();
    let expected = base.derivatives::<4>(0.25, 0.375).unwrap();
    for scale in [2_f64.powi(-900), 2_f64.powi(1020), f64::from_bits(16)] {
        let s = NurbsSurface3::new(
            knots(1),
            knots(1),
            points.clone(),
            weights.iter().map(|w| w * scale).collect(),
        )
        .unwrap();
        assert_eq!(s.weights(), base.weights());
        assert_eq!(s.derivatives::<4>(0.25, 0.375).unwrap(), expected);
    }
    let translated = NurbsSurface3::new(
        knots(1),
        knots(1),
        points.iter().map(|p| p.map(|v| v + 1e12)).collect(),
        weights,
    )
    .unwrap();
    let actual = translated.derivatives::<4>(0.25, 0.375).unwrap();
    for k in 0..4 {
        for l in 0..4 {
            for axis in 0..3 {
                near(
                    actual[k][l][axis],
                    expected[k][l][axis] + if k + l == 0 { 1e12 } else { 0. },
                    1e-12,
                );
            }
        }
    }
    for scale in [1e-150, 1e150] {
        let s = NurbsSurface3::new(
            knots(1),
            knots(1),
            points.iter().map(|p| p.map(|v| v * scale)).collect(),
            base.weights().to_vec(),
        )
        .unwrap();
        let actual = s.derivatives::<4>(0.25, 0.375).unwrap();
        for k in 0..4 {
            for l in 0..4 {
                for axis in 0..3 {
                    near(
                        actual[k][l][axis],
                        expected[k][l][axis] * scale,
                        1e-12 * scale * expected[k][l][axis].abs().max(1.),
                    );
                }
            }
        }
    }
}

#[test]
fn tiny_active_weights_and_numerical_range_have_explicit_results() {
    let ku = KnotVector::new(1, vec![0., 0., 0.5, 1., 1.]).unwrap();
    let points = vec![[0., 0.], [0., 1.], [1., 0.], [1., 1.], [2., 0.], [2., 1.]];
    let s = NurbsSurface2::new(
        ku,
        knots(1),
        points,
        vec![
            f64::MIN_POSITIVE,
            f64::MIN_POSITIVE,
            f64::MIN_POSITIVE,
            f64::MIN_POSITIVE,
            1.,
            1.,
        ],
    )
    .unwrap();
    let d = s.derivatives::<3>(0.125, 0.375).unwrap();
    assert_eq!(d[0][0], [0.25, 0.375]);
    assert_eq!(d[1][0], [2., 0.]);
    let wide = NurbsSurface2::new(
        knots(1),
        knots(1),
        vec![
            [-f64::MAX, 0.],
            [-f64::MAX, 1.],
            [f64::MAX, 0.],
            [f64::MAX, 1.],
        ],
        vec![],
    )
    .unwrap();
    assert_eq!(wide.point(0.5, 0.5).unwrap(), [0., 0.5]);
    assert_eq!(
        wide.derivatives::<2>(0.5, 0.5),
        Err(MathError::NumericalRange)
    );
    let extreme = KnotVector::new(1, vec![-f64::MAX, -f64::MAX, f64::MAX, f64::MAX]).unwrap();
    let s = NurbsSurface2::new(extreme, knots(1), vec![[0.; 2]; 4], vec![]).unwrap();
    assert_eq!(s.point(0., 0.), Err(MathError::NumericalRange));
    assert_eq!(s.derivatives::<2>(0., 0.), Err(MathError::NumericalRange));
}

#[test]
fn underflowing_blends_fail_instead_of_returning_amplified_rounding() {
    // At u=v=1e-160 an 80-digit Decimal recurrence gives x=4.494232837153770e295.
    // The subnormal u*v loses significant bits before division by a tiny weight.
    // This arithmetic range is explicitly unsupported, never a silently precise fact.
    let s = NurbsSurface3::new(
        knots(1),
        knots(1),
        vec![[0.; 3], [0.; 3], [0.; 3], [1e308, 0., 0.]],
        vec![f64::MIN_POSITIVE, f64::MIN_POSITIVE, f64::MIN_POSITIVE, 1.],
    )
    .unwrap();
    assert_eq!(s.point(1e-160, 1e-160), Err(MathError::NumericalRange));
    assert_eq!(
        s.derivatives::<1>(1e-160, 1e-160),
        Err(MathError::NumericalRange)
    );
    assert_eq!(s.point(1e-170, 1e-170), Err(MathError::NumericalRange));
    assert_eq!(s.point(0., 0.).unwrap(), [0.; 3]);
    assert_eq!(s.point(1., 1.).unwrap(), [1e308, 0., 0.]);

    // A univariate basis term can underflow before tensor blending starts.
    let s = NurbsSurface3::new(
        knots(2),
        knots(1),
        vec![
            [0.; 3],
            [0.; 3],
            [0.; 3],
            [0.; 3],
            [1e308, 0., 0.],
            [1e308, 0., 0.],
        ],
        vec![
            f64::MIN_POSITIVE,
            f64::MIN_POSITIVE,
            f64::MIN_POSITIVE,
            f64::MIN_POSITIVE,
            1.,
            1.,
        ],
    )
    .unwrap();
    assert_eq!(s.point(1e-170, 0.5), Err(MathError::NumericalRange));
    assert_eq!(
        s.derivatives::<2>(1e-170, 0.5),
        Err(MathError::NumericalRange)
    );
}
