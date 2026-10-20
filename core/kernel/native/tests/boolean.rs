// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use nightfall_native::{
    Context, NativeError, boolean::BooleanOp, extrusion::*, mesh::MeshingOptions,
};
#[path = "support/boolean_fixture.rs"]
mod fixture;
fn context() -> Option<Context> {
    let value = Context::new(1e-6, 1e-9);
    if std::env::var("NF_NATIVE_EXPECT_OCCT").expect("run via CTest") == "0" {
        assert!(matches!(value, Err(NativeError::Unavailable)));
        None
    } else {
        Some(value.unwrap())
    }
}
fn inspect(
    result: &OperationResult,
    sources: &[&OperationResult],
    volume: f64,
    area: f64,
    solids: u64,
) {
    let m = result.shape.measures();
    assert!((m.volume - volume).abs() <= 1e-7);
    assert!((m.area - area).abs() <= 1e-7);
    assert_eq!(m.solids, solids);
    assert_eq!(
        result.history.len() as u64,
        sources
            .iter()
            .map(|s| s.shape.measures().faces + s.shape.measures().edges)
            .sum()
    );
    for source in sources {
        for face in &source.generated {
            assert_eq!(
                result
                    .history
                    .iter()
                    .filter(|h| h.before.same_as(&face.entity).unwrap())
                    .count(),
                1
            );
        }
    }
    for h in &result.history {
        assert_eq!(h.deleted, h.modified.is_empty());
        assert!(h.before.same_as(&h.before.clone()).unwrap());
        for entity in h.modified.iter().chain(&h.generated) {
            assert!(entity.same_as(&entity.clone()).unwrap());
        }
    }
}
#[test]
fn safe_booleans_have_analytic_measures_complete_history_and_unchanged_sources() {
    let Some(context) = context() else { return };
    let a = context
        .extrude(&fixture::box_input([0.; 3], [10.; 3]))
        .unwrap();
    let b = context
        .extrude(&fixture::box_input([5., 0., 0.], [10.; 3]))
        .unwrap();
    let before = (*a.shape.measures(), *b.shape.measures());
    let prior = context
        .export_step(&[a.shape.clone(), b.shape.clone()])
        .unwrap();
    let bytes = prior.bytes().to_vec();
    let mesh = context
        .tessellate(&a.shape, &MeshingOptions::default())
        .unwrap();
    let positions: Vec<_> = mesh.vertices().iter().map(|v| v.position).collect();
    for (op, volume, area) in [
        (BooleanOp::Union, 1500., 800.),
        (BooleanOp::Subtract, 500., 400.),
        (BooleanOp::Intersect, 500., 400.),
    ] {
        let result = context
            .boolean_operation(&a.shape, std::slice::from_ref(&b.shape), op)
            .unwrap();
        inspect(&result, &[&a, &b], volume, area, 1);
        assert_eq!(
            (result.shape.measures().faces, result.shape.measures().edges),
            (6, 12)
        );
        assert_eq!((*a.shape.measures(), *b.shape.measures()), before);
        assert_eq!(prior.bytes(), bytes);
    }
    assert_eq!(
        mesh.vertices()
            .iter()
            .map(|v| v.position)
            .collect::<Vec<_>>(),
        positions
    );
}
#[test]
fn multiple_and_duplicate_tools_preserve_union_semantics_and_split_solids() {
    let Some(context) = context() else { return };
    let a = context
        .extrude(&fixture::box_input([0.; 3], [10.; 3]))
        .unwrap();
    let b = context
        .extrude(&fixture::box_input([2., -1., -1.], [2., 12., 12.]))
        .unwrap();
    let c = context
        .extrude(&fixture::box_input([6., -1., -1.], [2., 12., 12.]))
        .unwrap();
    let tools = [b.shape.clone(), c.shape.clone()];
    for (op, volume, area, solids) in [
        (BooleanOp::Subtract, 600., 840., 3),
        (BooleanOp::Intersect, 400., 560., 2),
    ] {
        let result = context.boolean_operation(&a.shape, &tools, op).unwrap();
        inspect(&result, &[&a, &b, &c], volume, area, solids);
    }
    let repeated = context
        .boolean_operation(
            &a.shape,
            &[b.shape.clone(), b.shape.clone()],
            BooleanOp::Subtract,
        )
        .unwrap();
    inspect(&repeated, &[&a, &b], 800., 720., 2);
    let identical = context
        .boolean_operation(
            &a.shape,
            &[a.shape.clone(), a.shape.clone()],
            BooleanOp::Union,
        )
        .unwrap();
    inspect(&identical, &[&a], 1000., 600., 1);
}
#[test]
fn exact_rational_holes_keep_generated_intersection_provenance_and_retained_readers() {
    let Some(context) = context() else { return };
    let a = context
        .extrude(&fixture::box_input([0.; 3], [10.; 3]))
        .unwrap();
    let b = context.extrude(&fixture::cylinder_input()).unwrap();
    let mut result = context
        .boolean_operation(
            &a.shape,
            std::slice::from_ref(&b.shape),
            BooleanOp::Subtract,
        )
        .unwrap();
    let m = *result.shape.measures();
    assert!((m.volume - (1000. - 40. * std::f64::consts::PI)).abs() <= 1e-5);
    assert!((m.area - (600. + 32. * std::f64::consts::PI)).abs() <= 1e-5);
    assert_eq!(m.solids, 1);
    assert!(
        result
            .generated
            .iter()
            .any(|g| g.role == "intersection" && g.entity.kind() == EntityKind::Edge)
    );
    assert!(result.history.iter().any(|h| {
        b.generated
            .iter()
            .any(|g| h.before.same_as(&g.entity).unwrap())
            && !h.deleted
    }));
    let history = result.history.remove(0);
    let mesh = context
        .tessellate(&result.shape, &MeshingOptions::default())
        .unwrap();
    let step = context
        .export_step(std::slice::from_ref(&result.shape))
        .unwrap();
    drop(result);
    drop(a);
    drop(b);
    drop(context);
    assert!(history.before.same_as(&history.before).unwrap());
    for entity in history.modified.iter().chain(&history.generated) {
        assert!(entity.same_as(entity).unwrap());
    }
    assert_eq!(mesh.faces().len() as u64, m.faces);
    for face in mesh.faces() {
        assert!(face.face.same_as(&face.face).unwrap());
    }
    let text = std::str::from_utf8(step.bytes()).unwrap();
    assert!(text.contains("AP242") && text.contains("CYLINDRICAL_SURFACE"));
    assert!(!text.contains("TESSELLATED"));
}
#[test]
fn invalid_empty_and_foreign_operands_and_solver_failures_recover_atomically() {
    let Some(context) = context() else { return };
    let a = context
        .extrude(&fixture::box_input([0.; 3], [10.; 3]))
        .unwrap();
    let b = context
        .extrude(&fixture::box_input([20., 0., 0.], [10.; 3]))
        .unwrap();
    let foreign = Context::new(1e-6, 1e-9).unwrap();
    let other = foreign
        .extrude(&fixture::box_input([0.; 3], [10.; 3]))
        .unwrap();
    let before = *a.shape.measures();
    for tools in [
        vec![],
        vec![other.shape.clone()],
        vec![b.shape.clone(), other.shape.clone()],
        vec![other.shape.clone(), b.shape.clone()],
    ] {
        assert!(matches!(
            context.boolean_operation(&a.shape, &tools, BooleanOp::Union),
            Err(NativeError::InvalidArgument)
        ));
    }
    assert!(matches!(
        context.boolean_operation(
            &other.shape,
            std::slice::from_ref(&a.shape),
            BooleanOp::Union
        ),
        Err(NativeError::InvalidArgument)
    ));
    assert!(matches!(
        context.boolean_operation(
            &a.shape,
            std::slice::from_ref(&a.shape),
            BooleanOp::Subtract
        ),
        Err(NativeError::Geometry { .. })
    ));
    assert!(matches!(
        context.boolean_operation(
            &a.shape,
            std::slice::from_ref(&b.shape),
            BooleanOp::Intersect
        ),
        Err(NativeError::Geometry { .. })
    ));
    let recovered = context
        .boolean_operation(&a.shape, std::slice::from_ref(&b.shape), BooleanOp::Union)
        .unwrap();
    inspect(&recovered, &[&a, &b], 2000., 1200., 2);
    assert_eq!(*a.shape.measures(), before);
}
#[test]
fn independent_callers_keep_boolean_owners_local_and_send_only_owned_bytes() {
    if context().is_none() {
        return;
    }
    let workers: Vec<_> = (0..4)
        .map(|_| {
            std::thread::spawn(|| {
                let context = Context::new(1e-6, 1e-9).unwrap();
                let a = context
                    .extrude(&fixture::box_input([0.; 3], [10.; 3]))
                    .unwrap();
                let b = context
                    .extrude(&fixture::box_input([5., 0., 0.], [10.; 3]))
                    .unwrap();
                let result = context
                    .boolean_operation(&a.shape, &[b.shape], BooleanOp::Union)
                    .unwrap();
                let step = context.export_step(&[result.shape]).unwrap();
                step.bytes().to_vec()
            })
        })
        .collect();
    for worker in workers {
        assert!(worker.join().unwrap().starts_with(b"ISO-10303-21;"));
    }
}
