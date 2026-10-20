// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use nightfall_kernel::*;
#[path = "support/mesh_fixture.rs"]
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
fn failure(at: MeshLocation, problem: MeshProblem) -> KernelError {
    KernelError::MeshInput { at, problem }
}
#[test]
fn typed_mesh_returns_exact_box_and_retains_original_picking_after_every_owner_drops() {
    let Some(kernel) = kernel() else { return };
    let source = kernel.extrude(&box_input()).unwrap();
    let mesh = kernel
        .tessellate(&source.shape, &MeshingOptions::default())
        .unwrap();
    assert_eq!(
        (
            mesh.vertices().len(),
            mesh.indices().len(),
            mesh.faces().len()
        ),
        (24, 36, 6)
    );
    for face in mesh.faces() {
        assert_eq!(
            source
                .generated
                .iter()
                .filter(|g| face.face.same_as(&g.entity).unwrap())
                .count(),
            1
        );
    }
    let exact = *source.shape.measures();
    let face = mesh.faces()[0].face.clone();
    let original = source
        .generated
        .iter()
        .find(|g| face.same_as(&g.entity).unwrap())
        .unwrap()
        .entity
        .clone();
    drop(source);
    drop(kernel);
    let (v, a) = fixture::inspect(&mesh, &exact);
    assert!((v - 6000.).abs() <= 1e-7);
    assert!((a - 2200.).abs() <= 1e-7);
    drop(mesh);
    assert!(face.same_as(&original).unwrap());
}
#[test]
fn deflection_checks_report_nonfinite_values_and_configured_precision_floors_before_native_access()
{
    let Some(kernel) = kernel() else { return };
    let source = kernel.extrude(&box_input()).unwrap();
    for bad in [f64::NAN, f64::INFINITY, f64::NEG_INFINITY] {
        for (at, options) in [
            (
                MeshLocation::LinearDeflection,
                MeshingOptions {
                    linear_deflection: bad,
                    ..MeshingOptions::default()
                },
            ),
            (
                MeshLocation::AngularDeflection,
                MeshingOptions {
                    angular_deflection: bad,
                    ..MeshingOptions::default()
                },
            ),
        ] {
            let expected = failure(at, MeshProblem::NonFinite);
            assert_eq!(kernel.validate_meshing(&options), Err(expected));
            assert!(matches!(kernel.tessellate(&source.shape,&options),Err(e) if e == expected));
        }
    }
    let precise = Kernel::new(2e-5, 5e-7).unwrap();
    let options = MeshingOptions {
        linear_deflection: 2e-5,
        angular_deflection: 5e-7,
        ..MeshingOptions::default()
    };
    assert_eq!(precise.validate_meshing(&options), Ok(()));
    assert_eq!(
        precise.validate_meshing(&MeshingOptions {
            linear_deflection: 1.9e-5,
            ..options
        }),
        Err(failure(
            MeshLocation::LinearDeflection,
            MeshProblem::BelowTolerance
        ))
    );
    assert_eq!(
        precise.validate_meshing(&MeshingOptions {
            angular_deflection: 4.9e-7,
            ..options
        }),
        Err(failure(
            MeshLocation::AngularDeflection,
            MeshProblem::BelowTolerance
        ))
    );
    for bad in [0., -1., 1e-10] {
        assert_eq!(
            kernel.validate_meshing(&MeshingOptions {
                linear_deflection: bad,
                ..MeshingOptions::default()
            }),
            Err(failure(
                MeshLocation::LinearDeflection,
                MeshProblem::BelowTolerance
            ))
        );
        assert_eq!(
            kernel.validate_meshing(&MeshingOptions {
                angular_deflection: bad,
                ..MeshingOptions::default()
            }),
            Err(failure(
                MeshLocation::AngularDeflection,
                MeshProblem::BelowTolerance
            ))
        );
    }
}
#[test]
fn mesh_limits_preserve_native_budget_errors_and_recover_at_the_exact_boundary() {
    let Some(kernel) = kernel() else { return };
    let source = kernel.extrude(&box_input()).unwrap();
    let before = *source.shape.measures();
    for bad in [0, 1_000_001, u64::MAX] {
        let options = MeshingOptions {
            max_triangles: bad,
            ..MeshingOptions::default()
        };
        let expected = failure(MeshLocation::TriangleLimit, MeshProblem::OutOfRange);
        assert_eq!(kernel.validate_meshing(&options), Err(expected));
        assert!(matches!(kernel.tessellate(&source.shape,&options),Err(e) if e == expected));
    }
    assert_eq!(
        kernel.validate_meshing(&MeshingOptions {
            angular_deflection: 4.,
            ..MeshingOptions::default()
        }),
        Err(failure(
            MeshLocation::AngularDeflection,
            MeshProblem::OutOfRange
        ))
    );
    assert!(matches!(
        kernel.tessellate(
            &source.shape,
            &MeshingOptions {
                max_triangles: 11,
                ..MeshingOptions::default()
            }
        ),
        Err(KernelError::Native(NativeError::NativeFailure))
    ));
    let options = MeshingOptions {
        linear_deflection: 1e-6,
        angular_deflection: std::f64::consts::PI,
        max_triangles: 12,
    };
    assert_eq!(kernel.validate_meshing(&options), Ok(()));
    let mesh = kernel.tessellate(&source.shape, &options).unwrap();
    assert_eq!(mesh.indices().len(), 36);
    assert_eq!(*source.shape.measures(), before);
}
#[test]
fn foreign_kernel_meshes_fail_without_invalidating_the_originating_snapshot() {
    let Some(kernel) = kernel() else { return };
    let source = kernel.extrude(&box_input()).unwrap();
    let foreign = Kernel::new(1e-6, 1e-9).unwrap();
    assert!(matches!(
        foreign.tessellate(&source.shape, &MeshingOptions::default()),
        Err(KernelError::Native(NativeError::InvalidArgument))
    ));
    assert_eq!(
        kernel
            .tessellate(&source.shape, &MeshingOptions::default())
            .unwrap()
            .faces()
            .len(),
        6
    );
}
#[test]
fn six_placed_rational_parts_have_complete_original_face_coverage_and_converged_meshes() {
    if kernel().is_none() {
        return;
    }
    let (identity, parts) = fixture::build().unwrap();
    assert!(identity.contains("policy=extrusion-rigid-mesh-step-v1;"));
    assert_eq!(parts.len(), 6);
    assert_eq!(
        parts.iter().map(|p| p.mesh.faces().len()).sum::<usize>(),
        84
    );
    let picked: Vec<_> = parts
        .iter()
        .map(|p| p.mesh.faces()[0].face.clone())
        .collect();
    drop(parts);
    for face in picked {
        assert!(face.same_as(&face.clone()).unwrap());
    }
}
#[test]
fn mesh_diagnostics_are_structured_and_readable() {
    let value = failure(MeshLocation::TriangleLimit, MeshProblem::OutOfRange);
    assert_eq!(value.to_string(), "mesh input TriangleLimit: OutOfRange");
    fn is_error<T: std::error::Error>(_: &T) {}
    is_error(&value);
}
