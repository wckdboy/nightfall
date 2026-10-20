// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use nightfall_kernel::*;
#[path = "support/step_fixture.rs"]
mod fixture;
use fixture::plate;
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
fn inspect(bytes: &[u8], bodies: usize) {
    let text = std::str::from_utf8(bytes).unwrap();
    assert!(text.starts_with("ISO-10303-21;"));
    assert!(text.trim_end().ends_with("END-ISO-10303-21;"));
    assert!(text.contains("AP242"));
    assert!(text.contains(".MILLI.,.METRE."));
    assert!(text.contains("CYLINDRICAL_SURFACE"));
    assert!(!text.contains("TESSELLATED"));
    assert_eq!(text.matches("MANIFOLD_SOLID_BREP(").count(), bodies);
}
#[test]
fn typed_step_bytes_and_clones_survive_all_geometry_and_facade_owners() {
    let Some(kernel) = kernel() else { return };
    let input = plate::input();
    let source = kernel.extrude(&input).unwrap();
    let original = kernel
        .export_step(std::slice::from_ref(&source.shape))
        .unwrap();
    let retained = original.clone();
    assert_eq!(original.bytes().as_ptr(), retained.bytes().as_ptr());
    let copy = original.bytes().to_vec();
    drop(original);
    drop(source);
    drop(input);
    drop(kernel);
    assert_eq!(retained.bytes(), copy);
    inspect(retained.bytes(), 1);
    drop(retained);
    std::thread::spawn(move || inspect(&copy, 1))
        .join()
        .unwrap();
}
#[test]
fn empty_and_mixed_context_exports_fail_without_damaging_sources_or_prior_bytes() {
    let Some(kernel) = kernel() else { return };
    let source = kernel.extrude(&plate::input()).unwrap();
    let before = *source.shape.measures();
    let entity = source.generated[2].entity.clone();
    let mesh = kernel
        .tessellate(&source.shape, &MeshingOptions::default())
        .unwrap();
    let positions: Vec<_> = mesh.vertices().iter().map(|v| v.position).collect();
    let prior = kernel
        .export_step(std::slice::from_ref(&source.shape))
        .unwrap();
    let bytes = prior.bytes().to_vec();
    let foreign = Kernel::new(1e-6, 1e-9).unwrap();
    let other = foreign.extrude(&plate::input()).unwrap();
    for inputs in [
        vec![],
        vec![other.shape.clone()],
        vec![source.shape.clone(), other.shape.clone()],
        vec![other.shape.clone(), source.shape.clone()],
    ] {
        assert!(matches!(
            kernel.export_step(&inputs),
            Err(KernelError::Native(NativeError::InvalidArgument))
        ));
    }
    assert_eq!(*source.shape.measures(), before);
    assert!(entity.same_as(&source.generated[2].entity).unwrap());
    assert_eq!(
        mesh.vertices()
            .iter()
            .map(|v| v.position)
            .collect::<Vec<_>>(),
        positions
    );
    assert_eq!(prior.bytes(), bytes);
    let recovered = kernel
        .export_step(std::slice::from_ref(&source.shape))
        .unwrap();
    inspect(recovered.bytes(), 1);
    assert!((source.shape.measures().volume - (9600. - 256. * std::f64::consts::PI)).abs() <= 1e-5);
    assert!((source.shape.measures().area - (3520. + 64. * std::f64::consts::PI)).abs() <= 1e-5);
}
#[test]
fn coincident_and_placed_sources_keep_every_body_without_mutating_history() {
    let Some(kernel) = kernel() else { return };
    let source = kernel.extrude(&plate::input()).unwrap();
    let placed = kernel
        .transform(
            &source.shape,
            &RigidTransform {
                translation: [17., -12., 9.],
                axis_origin: [2., 1., 3.],
                axis_direction: [0., 0., 5.],
                angle: std::f64::consts::FRAC_PI_2,
            },
        )
        .unwrap();
    let before = *placed.shape.measures();
    for (actual, expected) in before.centroid.into_iter().zip([20., -13., 13.]) {
        assert!((actual - expected).abs() <= 1e-8);
    }
    let history: Vec<_> = placed
        .history
        .iter()
        .map(|h| (h.modified.len(), h.generated.len()))
        .collect();
    let step = kernel
        .export_step(&[
            source.shape.clone(),
            source.shape.clone(),
            placed.shape.clone(),
        ])
        .unwrap();
    assert_eq!(*placed.shape.measures(), before);
    assert_eq!(
        placed
            .history
            .iter()
            .map(|h| (h.modified.len(), h.generated.len()))
            .collect::<Vec<_>>(),
        history
    );
    drop(source);
    drop(placed);
    drop(kernel);
    inspect(step.bytes(), 3);
}
#[test]
fn six_placed_rational_plates_export_through_the_typed_facade() {
    if kernel().is_none() {
        return;
    }
    let (identity, step) = fixture::build().unwrap();
    assert!(identity.contains("policy=extrusion-rigid-mesh-step-boolean-v1;"));
    inspect(step.bytes(), 6);
}
#[test]
fn reexported_step_remains_neither_send_nor_sync() {
    trait IfSend<A> {
        fn check() {}
    }
    impl<T: ?Sized> IfSend<()> for T {}
    impl<T: ?Sized + Send> IfSend<u8> for T {}
    let _ = <Step as IfSend<_>>::check;
    trait IfSync<A> {
        fn check() {}
    }
    impl<T: ?Sized> IfSync<()> for T {}
    impl<T: ?Sized + Sync> IfSync<u8> for T {}
    let _ = <Step as IfSync<_>>::check;
}
