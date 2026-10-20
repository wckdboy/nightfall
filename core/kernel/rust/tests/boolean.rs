// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use nightfall_kernel::*;
#[path = "../../native/tests/support/boolean_fixture.rs"]
mod fixture;
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
#[test]
fn typed_booleans_preserve_native_goldens_and_retained_history_mesh_and_step() {
    let Some(kernel) = kernel() else { return };
    let a = kernel
        .extrude(&fixture::box_input([0.; 3], [10.; 3]))
        .unwrap();
    let b = kernel
        .extrude(&fixture::box_input([5., 0., 0.], [10.; 3]))
        .unwrap();
    let before = (*a.shape.measures(), *b.shape.measures());
    let mut results = Vec::new();
    for (mode, volume, area) in [
        (BooleanOp::Union, 1500., 800.),
        (BooleanOp::Subtract, 500., 400.),
        (BooleanOp::Intersect, 500., 400.),
    ] {
        let result = kernel
            .boolean_operation(&a.shape, std::slice::from_ref(&b.shape), mode)
            .unwrap();
        let m = result.shape.measures();
        assert!((m.volume - volume).abs() <= 1e-7);
        assert!((m.area - area).abs() <= 1e-7);
        assert_eq!((m.solids, m.faces, m.edges), (1, 6, 12));
        assert_eq!(result.history.len(), 36);
        assert_eq!((*a.shape.measures(), *b.shape.measures()), before);
        let mesh = kernel
            .tessellate(&result.shape, &MeshingOptions::default())
            .unwrap();
        let step = kernel
            .export_step(std::slice::from_ref(&result.shape))
            .unwrap();
        results.push((result, mesh, step));
    }
    drop(a);
    drop(b);
    drop(kernel);
    for (result, mesh, step) in results {
        for row in result.history {
            assert!(row.before.same_as(&row.before).unwrap());
            for entity in row.modified.iter().chain(&row.generated) {
                assert!(entity.same_as(entity).unwrap());
            }
        }
        drop(result.shape);
        assert_eq!(mesh.faces().len(), 6);
        for face in mesh.faces() {
            assert!(face.face.same_as(&face.face).unwrap());
        }
        assert!(step.bytes().starts_with(b"ISO-10303-21;"));
    }
}
#[test]
fn typed_native_errors_reject_empty_and_foreign_tools_and_preserve_recovery() {
    let Some(kernel) = kernel() else { return };
    let a = kernel
        .extrude(&fixture::box_input([0.; 3], [10.; 3]))
        .unwrap();
    let b = kernel
        .extrude(&fixture::box_input([20., 0., 0.], [10.; 3]))
        .unwrap();
    let other = Kernel::new(1e-6, 1e-9).unwrap();
    let c = other
        .extrude(&fixture::box_input([0.; 3], [10.; 3]))
        .unwrap();
    let prior = kernel.export_step(std::slice::from_ref(&a.shape)).unwrap();
    let bytes = prior.bytes().to_vec();
    for tools in [
        vec![],
        vec![c.shape.clone()],
        vec![b.shape.clone(), c.shape.clone()],
    ] {
        assert!(matches!(
            kernel.boolean_operation(&a.shape, &tools, BooleanOp::Union),
            Err(KernelError::Native(NativeError::InvalidArgument))
        ));
    }
    assert!(matches!(
        kernel.boolean_operation(
            &a.shape,
            std::slice::from_ref(&b.shape),
            BooleanOp::Intersect
        ),
        Err(KernelError::Native(NativeError::Geometry { .. }))
    ));
    let recovered = kernel
        .boolean_operation(
            &a.shape,
            &[a.shape.clone(), a.shape.clone()],
            BooleanOp::Union,
        )
        .unwrap();
    assert!((recovered.shape.measures().volume - 1000.).abs() <= 1e-7);
    assert_eq!(recovered.history.len(), 18);
    assert_eq!(prior.bytes(), bytes);
}
#[test]
fn typed_rational_hole_preserves_exact_curves_and_intersection_provenance() {
    let Some(kernel) = kernel() else { return };
    let a = kernel
        .extrude(&fixture::box_input([0.; 3], [10.; 3]))
        .unwrap();
    let b = kernel.extrude(&fixture::cylinder_input()).unwrap();
    let result = kernel
        .boolean_operation(&a.shape, &[b.shape], BooleanOp::Subtract)
        .unwrap();
    let m = result.shape.measures();
    assert!((m.volume - (1000. - 40. * std::f64::consts::PI)).abs() <= 1e-5);
    assert!((m.area - (600. + 32. * std::f64::consts::PI)).abs() <= 1e-5);
    assert!(
        result
            .generated
            .iter()
            .any(|g| g.role == "intersection" && g.entity.kind() == EntityKind::Edge)
    );
    let step = kernel.export_step(&[result.shape]).unwrap();
    assert!(
        std::str::from_utf8(step.bytes())
            .unwrap()
            .contains("CYLINDRICAL_SURFACE")
    );
}
