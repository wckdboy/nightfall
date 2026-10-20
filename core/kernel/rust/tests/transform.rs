// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use nightfall_kernel::*;
fn kernel() -> Option<Kernel> {
    let value = Kernel::new(1e-6, 1e-9);
    if std::env::var("NF_NATIVE_EXPECT_OCCT").expect("run via CTest") == "0" {
        assert!(matches!(
            value,
            Err(KernelError::Native(NativeError::Unavailable))
        ));
        None
    } else {
        Some(value.unwrap())
    }
}
fn input() -> Extrusion {
    let points = [[0., 0.], [10., 0.], [10., 20.], [0., 20.], [0., 0.]];
    let outer = points
        .windows(2)
        .map(|p| {
            NurbsCurve2::new(
                KnotVector::new(1, vec![0., 0., 1., 1.]).unwrap(),
                p.to_vec(),
                vec![],
            )
            .unwrap()
        })
        .collect();
    Extrusion {
        frame: Frame::default(),
        displacement: [0., 0., 30.],
        regions: vec![Region {
            outer,
            holes: vec![],
        }],
    }
}
#[test]
fn typed_rigid_placement_preserves_geometry_and_complete_face_edge_history() {
    let Some(kernel) = kernel() else { return };
    let source = kernel.extrude(&input()).unwrap();
    let transform = RigidTransform {
        translation: [7., -2., 4.],
        axis_origin: [2., 3., 0.],
        axis_direction: [0., 0., 5.],
        angle: std::f64::consts::FRAC_PI_2,
    };
    let result = kernel.transform(&source.shape, &transform).unwrap();
    let m = result.shape.measures();
    assert!((m.volume - 6000.).abs() <= 1e-8);
    assert!((m.area - 2200.).abs() <= 1e-8);
    assert_eq!((m.solids, m.faces, m.edges), (1, 6, 12));
    for (actual, expected) in m.centroid.into_iter().zip([2., 4., 19.]) {
        assert!((actual - expected).abs() <= 1e-8);
    }
    assert!(result.generated.is_empty());
    assert_eq!(result.history.len(), 18);
    assert_eq!(
        result
            .history
            .iter()
            .filter(|h| h.before.kind() == EntityKind::Face)
            .count(),
        6
    );
    assert_eq!(
        result
            .history
            .iter()
            .filter(|h| h.before.kind() == EntityKind::Edge)
            .count(),
        12
    );
    for row in &result.history {
        assert!(!row.deleted);
        assert!(row.generated.is_empty());
        assert_eq!(row.modified.len(), 1);
        assert_eq!(row.before.kind(), row.modified[0].kind());
    }
    for g in &source.generated {
        assert_eq!(
            result
                .history
                .iter()
                .filter(|h| h.before.same_as(&g.entity).unwrap())
                .count(),
            1
        );
    }
    assert!((source.shape.measures().centroid[0] - 5.).abs() <= 1e-10);
}
#[test]
fn out_of_plane_rotation_uses_the_explicit_offset_axis_before_translation() {
    let Some(kernel) = kernel() else { return };
    let source = kernel.extrude(&input()).unwrap();
    let pose = RigidTransform {
        translation: [-4., 3., 7.],
        axis_origin: [0., 2., 3.],
        axis_direction: [5., 0., 0.],
        angle: std::f64::consts::FRAC_PI_2,
    };
    let result = kernel.transform(&source.shape, &pose).unwrap();
    let m = result.shape.measures();
    assert!((m.volume - 6000.).abs() <= 1e-8);
    assert!((m.area - 2200.).abs() <= 1e-8);
    for (actual, expected) in m.centroid.into_iter().zip([1., -7., 18.]) {
        assert!((actual - expected).abs() <= 1e-8);
    }
    for (actual, expected) in m.minimum.into_iter().zip([-4., -22., 8.]) {
        assert!((actual - expected).abs() <= 2e-7);
    }
    for (actual, expected) in m.maximum.into_iter().zip([6., 8., 28.]) {
        assert!((actual - expected).abs() <= 2e-7);
    }
}
#[test]
fn transform_input_diagnostics_identify_every_nonfinite_field_and_zero_axis() {
    let Some(kernel) = kernel() else { return };
    let source = kernel.extrude(&input()).unwrap();
    for bad in [f64::NAN, f64::INFINITY, f64::NEG_INFINITY] {
        for field in 0..10 {
            let mut input = RigidTransform::default();
            let at = match field {
                0..=2 => {
                    input.translation[field] = bad;
                    TransformLocation::Translation(field)
                }
                3..=5 => {
                    input.axis_origin[field - 3] = bad;
                    TransformLocation::AxisOrigin(field - 3)
                }
                6..=8 => {
                    input.axis_direction[field - 6] = bad;
                    TransformLocation::AxisDirection(field - 6)
                }
                _ => {
                    input.angle = bad;
                    TransformLocation::Angle
                }
            };
            let expected = KernelError::TransformInput {
                at,
                problem: TransformProblem::NonFinite,
            };
            assert_eq!(kernel.validate_transform(&input), Err(expected.clone()));
            assert!(
                matches!(kernel.transform(&source.shape, &input), Err(error) if error == expected)
            );
        }
    }
    for angle in [0., 0.5] {
        let input = RigidTransform {
            axis_direction: [0., -0., 0.],
            angle,
            ..RigidTransform::default()
        };
        let expected = KernelError::TransformInput {
            at: TransformLocation::Axis,
            problem: TransformProblem::ZeroAxis,
        };
        assert_eq!(kernel.validate_transform(&input), Err(expected.clone()));
        assert!(matches!(kernel.transform(&source.shape, &input), Err(error) if error == expected));
    }
}
#[test]
fn native_axis_precision_floor_stays_configured_and_input_direction_is_preserved() {
    let Some(kernel) = kernel() else { return };
    let source = kernel.extrude(&input()).unwrap();
    for length in [1e-6, 2e-6] {
        let input = RigidTransform {
            axis_direction: [0., 0., length],
            ..RigidTransform::default()
        };
        assert_eq!(kernel.validate_transform(&input), Ok(()));
        let result = kernel.transform(&source.shape, &input);
        if length == 1e-6 {
            assert!(matches!(
                result,
                Err(KernelError::Native(NativeError::Geometry { .. }))
            ));
        } else {
            assert!((result.unwrap().shape.measures().volume - 6000.).abs() <= 1e-8);
        }
        assert_eq!(input.axis_direction, [0., 0., length]);
    }
    let larger = Kernel::new(2e-6, 1e-9).unwrap();
    let source = larger.extrude(&input()).unwrap();
    let pose = RigidTransform {
        axis_direction: [0., 0., 2e-6],
        ..RigidTransform::default()
    };
    assert!(matches!(
        larger.transform(&source.shape, &pose),
        Err(KernelError::Native(NativeError::Geometry { .. }))
    ));
}
#[test]
fn retained_kernel_history_survives_every_facade_and_foreign_contexts_fail() {
    let Some(kernel) = kernel() else { return };
    let source = kernel.extrude(&input()).unwrap();
    let other = Kernel::new(1e-6, 1e-9).unwrap();
    assert!(matches!(
        other.transform(&source.shape, &RigidTransform::default()),
        Err(KernelError::Native(NativeError::InvalidArgument))
    ));
    let first = kernel
        .transform(
            &source.shape,
            &RigidTransform {
                translation: [1., 2., 3.],
                ..RigidTransform::default()
            },
        )
        .unwrap();
    let second = kernel
        .transform(
            &first.shape,
            &RigidTransform {
                translation: [-1., -2., -3.],
                ..RigidTransform::default()
            },
        )
        .unwrap();
    let before = second.history[0].before.clone();
    let after = second.history[0].modified[0].clone();
    let shape = second.shape.clone();
    drop(source);
    drop(first);
    drop(second);
    drop(kernel);
    drop(other);
    assert!((shape.measures().volume - 6000.).abs() <= 1e-8);
    for (actual, expected) in shape.measures().centroid.into_iter().zip([5., 10., 15.]) {
        assert!((actual - expected).abs() <= 1e-8);
    }
    drop(shape);
    assert!(before.same_as(&before.clone()).unwrap());
    assert!(after.same_as(&after.clone()).unwrap());
    assert!(!before.same_as(&after).unwrap());
}
