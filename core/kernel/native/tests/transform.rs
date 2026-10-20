// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use nightfall_math::{KnotVector, NurbsCurve2};
use nightfall_native::{Context, NativeError, extrusion::*, transform::RigidTransform};
fn context() -> Option<Context> {
    let value = Context::new(1e-6, 1e-9);
    if std::env::var("NF_NATIVE_EXPECT_OCCT").expect("run via CTest") == "0" {
        assert!(matches!(value, Err(NativeError::Unavailable)));
        None
    } else {
        Some(value.unwrap())
    }
}
fn input() -> Extrusion {
    let points = [[0., 0.], [10., 0.], [10., 20.], [0., 20.], [0., 0.]];
    let outer = points
        .windows(2)
        .map(|pair| {
            NurbsCurve2::new(
                KnotVector::new(1, vec![0., 0., 1., 1.]).unwrap(),
                pair.to_vec(),
                vec![],
            )
            .unwrap()
        })
        .collect();
    Extrusion {
        frame: Frame::default(),
        regions: vec![Region {
            outer,
            holes: vec![],
        }],
        displacement: [0., 0., 30.],
    }
}
fn assert_measures(shape: &Shape, centroid: [f64; 3]) {
    let m = shape.measures();
    assert!((m.volume - 6000.).abs() <= 1e-8);
    assert!((m.area - 2200.).abs() <= 1e-8);
    assert_eq!((m.solids, m.faces, m.edges), (1, 6, 12));
    for (actual, expected) in m.centroid.into_iter().zip(centroid) {
        assert!((actual - expected).abs() <= 1e-8);
    }
}
#[test]
fn safe_transform_returns_exact_measures_and_complete_face_edge_history() {
    let Some(context) = context() else { return };
    let source = context.extrude(&input()).unwrap();
    let input = RigidTransform {
        translation: [7., 0., 0.],
        angle: std::f64::consts::FRAC_PI_2,
        ..RigidTransform::default()
    };
    let copy = input;
    let result = context.transform(&source.shape, &input).unwrap();
    assert_eq!(input, copy);
    assert_measures(&result.shape, [-3., 5., 15.]);
    assert_measures(&source.shape, [5., 10., 15.]);
    assert!(result.generated.is_empty());
    assert!(result.estimated_bytes > 0);
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
        assert_eq!(row.modified.len(), 1);
        assert!(row.generated.is_empty());
        assert!(!row.deleted);
        assert_eq!(row.before.kind(), row.modified[0].kind());
        assert!(!row.before.same_as(&row.modified[0]).unwrap());
    }
    for generated in &source.generated {
        assert_eq!(
            result
                .history
                .iter()
                .filter(|h| h.before.same_as(&generated.entity).unwrap())
                .count(),
            1
        );
    }
}
#[test]
fn offset_axis_nonunit_and_identity_follow_the_original_transform_order() {
    let Some(context) = context() else { return };
    let source = context.extrude(&input()).unwrap();
    let moved = context
        .transform(
            &source.shape,
            &RigidTransform {
                translation: [7., -2., 4.],
                axis_origin: [2., 3., 0.],
                axis_direction: [0., 0., 5.],
                angle: std::f64::consts::FRAC_PI_2,
            },
        )
        .unwrap();
    assert_measures(&moved.shape, [2., 4., 19.]);
    for (actual, expected) in moved
        .shape
        .measures()
        .minimum
        .into_iter()
        .zip([-8., -1., 4.])
    {
        assert!((actual - expected).abs() <= 2e-7);
    }
    for (actual, expected) in moved
        .shape
        .measures()
        .maximum
        .into_iter()
        .zip([12., 9., 34.])
    {
        assert!((actual - expected).abs() <= 2e-7);
    }
    let identity = context
        .transform(&source.shape, &RigidTransform::default())
        .unwrap();
    assert_measures(&identity.shape, [5., 10., 15.]);
    assert_eq!(identity.history.len(), 18);
    assert!(
        !identity.history[0]
            .before
            .same_as(&identity.history[0].modified[0])
            .unwrap()
    );
}
#[test]
fn retained_history_survives_source_intermediate_context_and_result_teardown() {
    let Some(context) = context() else { return };
    let source = context.extrude(&input()).unwrap();
    let first = context
        .transform(
            &source.shape,
            &RigidTransform {
                translation: [1., 2., 3.],
                ..RigidTransform::default()
            },
        )
        .unwrap();
    let before = first.history[0].before.clone();
    let after = first.history[0].modified[0].clone();
    let second = context
        .transform(
            &first.shape,
            &RigidTransform {
                translation: [-1., -2., -3.],
                ..RigidTransform::default()
            },
        )
        .unwrap();
    assert_eq!(
        second
            .history
            .iter()
            .filter(|h| h.before.same_as(&after).unwrap())
            .count(),
        1
    );
    let shape = second.shape.clone();
    let last = second.history[0].modified[0].clone();
    drop(source);
    drop(first);
    drop(second);
    drop(context);
    assert_measures(&shape, [5., 10., 15.]);
    drop(shape);
    assert!(before.same_as(&before.clone()).unwrap());
    assert!(after.same_as(&after.clone()).unwrap());
    assert!(last.same_as(&last.clone()).unwrap());
    assert!(!before.same_as(&last).unwrap());
}
#[test]
fn foreign_contexts_and_invalid_transform_inputs_leave_the_source_usable() {
    let Some(context) = context() else { return };
    let source = context.extrude(&input()).unwrap();
    let foreign = Context::new(1e-6, 1e-9).unwrap();
    assert!(matches!(
        foreign.transform(&source.shape, &RigidTransform::default()),
        Err(NativeError::InvalidArgument)
    ));
    for value in [f64::NAN, f64::INFINITY, f64::NEG_INFINITY] {
        for field in 0..10 {
            let mut input = RigidTransform::default();
            match field {
                0..=2 => input.translation[field] = value,
                3..=5 => input.axis_origin[field - 3] = value,
                6..=8 => input.axis_direction[field - 6] = value,
                _ => input.angle = value,
            };
            assert!(matches!(
                context.transform(&source.shape, &input),
                Err(NativeError::NativeFailure)
            ));
        }
    }
    let zero = RigidTransform {
        axis_direction: [0.; 3],
        ..RigidTransform::default()
    };
    assert!(matches!(
        context.transform(&source.shape, &zero),
        Err(NativeError::NativeFailure)
    ));
    assert_measures(&source.shape, [5., 10., 15.]);
    assert_measures(
        &context
            .transform(&source.shape, &RigidTransform::default())
            .unwrap()
            .shape,
        [5., 10., 15.],
    );
}
#[test]
fn independent_threads_keep_native_geometry_and_history_on_their_own_context() {
    let available = context().is_some();
    let transform = RigidTransform {
        translation: [7., 0., 0.],
        angle: std::f64::consts::FRAC_PI_2,
        ..RigidTransform::default()
    };
    let workers: Vec<_> = (0..8)
        .map(|_| {
            std::thread::spawn(move || {
                let Some(context) = context() else {
                    assert!(!available);
                    return;
                };
                let source = context.extrude(&input()).unwrap();
                for _ in 0..4 {
                    let result = context.transform(&source.shape, &transform).unwrap();
                    assert_measures(&result.shape, [-3., 5., 15.]);
                    assert_eq!(result.history.len(), 18);
                    assert!(
                        result.history[0]
                            .before
                            .same_as(&result.history[0].before)
                            .unwrap()
                    );
                }
            })
        })
        .collect();
    for worker in workers {
        worker.join().unwrap();
    }
}
