// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use nightfall_math::{KnotVector, NurbsCurve2};
use nightfall_native::{Context, NativeError, extrusion::*, step::Step, transform::RigidTransform};
fn context() -> Option<Context> {
    let value = Context::new(1e-6, 1e-9);
    if std::env::var("NF_NATIVE_EXPECT_OCCT").expect("run via CTest") == "0" {
        assert!(matches!(value, Err(NativeError::Unavailable)));
        None
    } else {
        Some(value.unwrap())
    }
}
fn box_input() -> Extrusion {
    let points = [[0., 0.], [10., 0.], [10., 20.], [0., 20.], [0., 0.]];
    Extrusion {
        frame: Frame::default(),
        displacement: [0., 0., 30.],
        regions: vec![Region {
            outer: points
                .windows(2)
                .map(|p| {
                    NurbsCurve2::new(
                        KnotVector::new(1, vec![0., 0., 1., 1.]).unwrap(),
                        p.to_vec(),
                        vec![],
                    )
                    .unwrap()
                })
                .collect(),
            holes: vec![],
        }],
    }
}
fn inspect(step: &Step, solids: usize) {
    let text = std::str::from_utf8(step.bytes()).unwrap();
    assert!(text.starts_with("ISO-10303-21;"));
    assert!(text.trim_end().ends_with("END-ISO-10303-21;"));
    assert!(text.contains("AP242"));
    assert!(!text.contains("TESSELLATED"));
    assert_eq!(text.matches("MANIFOLD_SOLID_BREP").count(), solids);
}
#[test]
fn safe_step_exports_exact_box_and_preserves_measures_entities_and_prior_buffers() {
    let Some(context) = context() else { return };
    let source = context.extrude(&box_input()).unwrap();
    let before = *source.shape.measures();
    assert!((before.volume - 6000.).abs() <= 1e-8);
    assert!((before.area - 2200.).abs() <= 1e-8);
    assert_eq!((before.solids, before.faces, before.edges), (1, 6, 12));
    let face = source.generated[0].entity.clone();
    let first = context
        .export_step(std::slice::from_ref(&source.shape))
        .unwrap();
    inspect(&first, 1);
    let prior = first.bytes().to_vec();
    let pointer = first.bytes().as_ptr();
    let second = context
        .export_step(&[source.shape.clone(), source.shape.clone()])
        .unwrap();
    inspect(&second, 2);
    drop(second);
    assert_eq!(first.bytes(), prior);
    assert_eq!(first.bytes().as_ptr(), pointer);
    assert_eq!(*source.shape.measures(), before);
    assert!(face.same_as(&source.generated[0].entity).unwrap());
}
#[test]
fn step_clones_share_buffers_after_every_geometry_owner_and_original_step_end() {
    let Some(context) = context() else { return };
    let source = context.extrude(&box_input()).unwrap();
    let step = context
        .export_step(std::slice::from_ref(&source.shape))
        .unwrap();
    let expected = step.bytes().to_vec();
    let cloned = step.clone();
    let pointer = step.bytes().as_ptr();
    assert_eq!(cloned.bytes().as_ptr(), pointer);
    drop(source);
    drop(context);
    drop(step);
    inspect(&cloned, 1);
    assert_eq!(cloned.bytes().as_ptr(), pointer);
    assert_eq!(cloned.bytes(), expected);
    // Ordinary copied bytes have no native thread affinity.
    let copied = cloned.bytes().to_vec();
    drop(cloned);
    assert_eq!(std::thread::spawn(move || copied).join().unwrap(), expected);
}
#[test]
fn empty_and_foreign_lists_fail_before_native_access_and_keep_prior_exports_usable() {
    let Some(context) = context() else { return };
    let source = context.extrude(&box_input()).unwrap();
    let foreign = Context::new(1e-6, 1e-9).unwrap();
    let other = foreign.extrude(&box_input()).unwrap();
    let prior = context
        .export_step(std::slice::from_ref(&source.shape))
        .unwrap();
    let bytes = prior.bytes().to_vec();
    for shapes in [
        vec![],
        vec![other.shape.clone()],
        vec![source.shape.clone(), other.shape.clone()],
    ] {
        assert!(matches!(
            context.export_step(&shapes),
            Err(NativeError::InvalidArgument)
        ));
    }
    assert_eq!(prior.bytes(), bytes);
    let recovered = context
        .export_step(std::slice::from_ref(&source.shape))
        .unwrap();
    inspect(&recovered, 1);
    assert!((source.shape.measures().volume - 6000.).abs() <= 1e-8);
}
#[test]
fn step_exports_located_and_coincident_solids_without_consuming_the_sources() {
    let Some(context) = context() else { return };
    let source = context.extrude(&box_input()).unwrap();
    let placed = context
        .transform(
            &source.shape,
            &RigidTransform {
                translation: [7., 2., 3.],
                axis_origin: [2., 1., 3.],
                axis_direction: [0., 0., 5.],
                angle: std::f64::consts::FRAC_PI_2,
            },
        )
        .unwrap();
    let before = *placed.shape.measures();
    for (a, b) in before.centroid.into_iter().zip([0., 6., 18.]) {
        assert!((a - b).abs() <= 1e-8);
    }
    let output = context
        .export_step(&[
            source.shape.clone(),
            placed.shape.clone(),
            source.shape.clone(),
        ])
        .unwrap();
    inspect(&output, 3);
    assert_eq!(*placed.shape.measures(), before);
    assert_eq!(placed.history.len(), 18);
    drop(source);
    drop(placed);
    drop(context);
    inspect(&output, 3);
}
#[test]
fn safe_step_retains_rational_holes_and_exact_analytic_cylinders() {
    let Some(context) = context() else { return };
    let circle = |r, sign| {
        NurbsCurve2::new(
            KnotVector::new(2, vec![0., 0., 0., 1., 1., 2., 2., 3., 3., 4., 4., 4.]).unwrap(),
            [
                [1., 0.],
                [1., 1.],
                [0., 1.],
                [-1., 1.],
                [-1., 0.],
                [-1., -1.],
                [0., -1.],
                [1., -1.],
                [1., 0.],
            ]
            .map(|p| [r * p[0], r * sign * p[1]])
            .to_vec(),
            vec![
                1.,
                std::f64::consts::FRAC_1_SQRT_2,
                1.,
                std::f64::consts::FRAC_1_SQRT_2,
                1.,
                std::f64::consts::FRAC_1_SQRT_2,
                1.,
                std::f64::consts::FRAC_1_SQRT_2,
                1.,
            ],
        )
        .unwrap()
    };
    let source = context
        .extrude(&Extrusion {
            frame: Frame::default(),
            displacement: [0., 0., 30.],
            regions: vec![Region {
                outer: vec![circle(10., 1.)],
                holes: vec![vec![circle(5., -1.)]],
            }],
        })
        .unwrap();
    let exact = *source.shape.measures();
    assert!((exact.volume - 2250. * std::f64::consts::PI).abs() <= 1e-6);
    assert!((exact.area - 1050. * std::f64::consts::PI).abs() <= 1e-6);
    let output = context
        .export_step(std::slice::from_ref(&source.shape))
        .unwrap();
    inspect(&output, 1);
    assert!(
        std::str::from_utf8(output.bytes())
            .unwrap()
            .contains("CYLINDRICAL_SURFACE")
    );
    assert_eq!(*source.shape.measures(), exact);
    drop(source);
    drop(context);
    inspect(&output, 1);
}
#[test]
fn independent_threads_export_and_release_only_their_own_native_buffers() {
    if context().is_none() {
        return;
    }
    let workers: Vec<_> = (0..4)
        .map(|_| {
            std::thread::spawn(|| {
                let context = Context::new(1e-6, 1e-9).unwrap();
                let source = context.extrude(&box_input()).unwrap();
                let step = context
                    .export_step(std::slice::from_ref(&source.shape))
                    .unwrap();
                drop(source);
                drop(context);
                inspect(&step, 1);
                step.bytes().to_vec()
            })
        })
        .collect();
    for worker in workers {
        let bytes = worker.join().unwrap();
        assert!(bytes.starts_with(b"ISO-10303-21;"));
    }
}
