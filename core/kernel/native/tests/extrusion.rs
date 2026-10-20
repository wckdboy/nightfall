// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use nightfall_math::{KnotVector, NurbsCurve2};
use nightfall_native::{Context, NativeError, extrusion::*};
fn context() -> Option<Context> {
    let value = Context::new(1e-6, 1e-9);
    if std::env::var("NF_NATIVE_EXPECT_OCCT").unwrap() == "0" {
        assert!(matches!(value, Err(NativeError::Unavailable)));
        None
    } else {
        Some(value.unwrap())
    }
}
fn line(a: [f64; 2], b: [f64; 2]) -> NurbsCurve2 {
    NurbsCurve2::new(
        KnotVector::new(1, vec![0.0, 0.0, 1.0, 1.0]).unwrap(),
        vec![a, b],
        vec![],
    )
    .unwrap()
}
fn box_input() -> Extrusion {
    Extrusion {
        frame: Frame::default(),
        displacement: [0.0, 0.0, 30.0],
        regions: vec![Region {
            outer: vec![
                line([0.0, 0.0], [10.0, 0.0]),
                line([10.0, 0.0], [10.0, 20.0]),
                line([10.0, 20.0], [0.0, 20.0]),
                line([0.0, 20.0], [0.0, 0.0]),
            ],
            holes: vec![],
        }],
    }
}
#[test]
fn solver_diagnostics_are_owned_and_survive_recovery_context_drop_and_threads() {
    let Some(context) = context() else { return };
    let mut input = box_input();
    input.frame.normal = [0., 0., 2.];
    let error = match context.extrude(&input) {
        Err(error) => error,
        Ok(_) => panic!("invalid plane accepted"),
    };
    let NativeError::Geometry { code, message } = &error else {
        panic!("lost solver diagnostic: {error:?}")
    };
    assert_eq!(code, "kernel");
    assert_eq!(message, "sketch plane must have finite orthonormal axes");
    input.frame = Frame::default();
    assert!(context.extrude(&input).is_ok());
    drop(context);
    std::thread::spawn(move || {
        let NativeError::Geometry { code, message } = error else {
            unreachable!()
        };
        assert_eq!(code, "kernel");
        assert_eq!(message, "sketch plane must have finite orthonormal axes");
    })
    .join()
    .unwrap();
}
fn circle(radius: f64, clockwise: bool) -> NurbsCurve2 {
    let sign = if clockwise { -1.0 } else { 1.0 };
    let w = std::f64::consts::FRAC_1_SQRT_2;
    let points = [
        [1.0, 0.0],
        [1.0, 1.0],
        [0.0, 1.0],
        [-1.0, 1.0],
        [-1.0, 0.0],
        [-1.0, -1.0],
        [0.0, -1.0],
        [1.0, -1.0],
        [1.0, 0.0],
    ];
    NurbsCurve2::new(
        KnotVector::new(
            2,
            vec![0.0, 0.0, 0.0, 1.0, 1.0, 2.0, 2.0, 3.0, 3.0, 4.0, 4.0, 4.0],
        )
        .unwrap(),
        points
            .map(|p| [p[0] * radius, p[1] * radius * sign])
            .to_vec(),
        vec![1.0, w, 1.0, w, 1.0, w, 1.0, w, 1.0],
    )
    .unwrap()
}
#[test]
fn safe_solids_and_entities_retain_every_owner_after_facade_teardown() {
    let Some(context) = context() else { return };
    let input = box_input();
    let result = context.extrude(&input).unwrap();
    assert!(result.history.is_empty());
    assert_eq!(result.generated.len(), 6);
    assert!(result.estimated_bytes > 0);
    for (i, g) in result.generated.iter().enumerate() {
        assert_eq!(g.entity.kind(), EntityKind::Face);
        assert_eq!(
            g.role,
            if i == 0 {
                "cap_start"
            } else if i == 1 {
                "cap_end"
            } else {
                "side"
            }
        );
        assert_eq!(
            g.from,
            if i < 2 {
                None
            } else {
                Some(ProfileEdge {
                    region: 0,
                    loop_index: 0,
                    curve: (i - 2) as u64,
                    section: 0,
                })
            }
        );
    }
    let shape = result.shape.clone();
    let face = result.generated[2].entity.clone();
    let cap = result.generated[0].entity.clone();
    drop(result);
    drop(input);
    drop(context);
    assert!((shape.measures().volume - 6000.0).abs() <= 1e-8);
    assert!((shape.measures().area - 2200.0).abs() <= 1e-8);
    assert_eq!(
        (
            shape.measures().solids,
            shape.measures().faces,
            shape.measures().edges
        ),
        (1, 6, 12)
    );
    drop(shape);
    assert!(face.same_as(&face.clone()).unwrap());
    assert!(!face.same_as(&cap).unwrap());
}
#[test]
fn typed_exact_curves_and_holes_preserve_sources_and_analytic_measures() {
    let Some(context) = context() else { return };
    let input = Extrusion {
        frame: Frame::default(),
        displacement: [0.0, 0.0, 5.0],
        regions: vec![Region {
            outer: vec![circle(10.0, false)],
            holes: vec![vec![circle(4.0, true)]],
        }],
    };
    let before: Vec<_> = input.regions[0].outer[0].points().to_vec();
    let result = context.extrude(&input).unwrap();
    let m = result.shape.measures();
    assert!((m.volume - 420.0 * std::f64::consts::PI).abs() <= 1e-5);
    assert!((m.area - 308.0 * std::f64::consts::PI).abs() <= 1e-5);
    assert_eq!(result.generated.len() as u64, m.faces);
    for g in &result.generated {
        if let Some(from) = g.from {
            assert_eq!(from.region, 0);
            assert_eq!(from.curve, 0);
            assert!(from.loop_index <= 1);
        }
    }
    assert_eq!(input.regions[0].outer[0].points(), before);
}
#[test]
fn placed_negative_and_oblique_inputs_preserve_typed_measurements() {
    let Some(context) = context() else { return };
    for variant in 0..2 {
        let mut input = box_input();
        let expected = if variant == 0 {
            input.frame = Frame {
                origin: [7.0, 2.0, 3.0],
                x_axis: [0.0, 1.0, 0.0],
                normal: [1.0, 0.0, 0.0],
            };
            input.displacement = [-30.0, 0.0, 0.0];
            [-8.0, 7.0, 13.0]
        } else {
            input.displacement = [4.0, -7.0, 30.0];
            [7.0, 6.5, 15.0]
        };
        let result = context.extrude(&input).unwrap();
        let m = result.shape.measures();
        assert!((m.volume - 6000.0).abs() <= 1e-8);
        for (actual, expected) in m.centroid.into_iter().zip(expected) {
            assert!((actual - expected).abs() <= 1e-8);
        }
    }
}
#[test]
fn foreign_contexts_fail_and_distinct_snapshots_compare_unequal() {
    let Some(first) = context() else { return };
    let second = Context::new(1e-6, 1e-9).unwrap();
    let a = first.extrude(&box_input()).unwrap();
    let b = first.extrude(&box_input()).unwrap();
    let c = second.extrude(&box_input()).unwrap();
    assert!(
        !a.generated[0]
            .entity
            .same_as(&b.generated[0].entity)
            .unwrap()
    );
    assert_eq!(
        a.generated[0].entity.same_as(&c.generated[0].entity),
        Err(NativeError::InvalidArgument)
    );
}
#[test]
fn rejected_inputs_publish_nothing_and_leave_retained_solids_usable() {
    let Some(context) = context() else { return };
    let original = context.extrude(&box_input()).unwrap();
    let mut input = box_input();
    input.displacement = [0.0; 3];
    assert!(context.extrude(&input).is_err());
    input = box_input();
    input.frame.normal = [f64::NAN, 0.0, 1.0];
    assert!(context.extrude(&input).is_err());
    input = box_input();
    input.regions[0].outer.pop();
    assert!(context.extrude(&input).is_err());
    input = box_input();
    input.regions.clear();
    assert!(context.extrude(&input).is_err());
    for _ in 0..64 {
        let result = context.extrude(&box_input()).unwrap();
        assert!((result.shape.measures().volume - 6000.0).abs() <= 1e-8);
    }
    assert!((original.shape.measures().volume - 6000.0).abs() <= 1e-8);
}
#[test]
fn native_shapes_entities_and_results_are_neither_send_nor_sync() {
    macro_rules! assert_local {
        ($ty:ty) => {{
            trait IfSend<A> {
                fn check() {}
            }
            impl<T: ?Sized> IfSend<()> for T {}
            impl<T: ?Sized + Send> IfSend<u8> for T {}
            let _ = <$ty as IfSend<_>>::check;
            trait IfSync<A> {
                fn check() {}
            }
            impl<T: ?Sized> IfSync<()> for T {}
            impl<T: ?Sized + Sync> IfSync<u8> for T {}
            let _ = <$ty as IfSync<_>>::check;
        }};
    }
    assert_local!(Shape);
    assert_local!(Entity);
    assert_local!(OperationResult);
}
