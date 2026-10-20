// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use nightfall_math::{KnotVector, NurbsCurve2};
use nightfall_native::{Context, NativeError, extrusion::*, mesh::*, transform::RigidTransform};
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
        regions: vec![Region {
            outer: points
                .windows(2)
                .map(|pair| {
                    NurbsCurve2::new(
                        KnotVector::new(1, vec![0., 0., 1., 1.]).unwrap(),
                        pair.to_vec(),
                        vec![],
                    )
                    .unwrap()
                })
                .collect(),
            holes: vec![],
        }],
        displacement: [0., 0., 30.],
    }
}
fn ring_input() -> Extrusion {
    let circle = |radius, sign| {
        let w = std::f64::consts::FRAC_1_SQRT_2;
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
            .map(|p| [p[0] * radius, p[1] * radius * sign])
            .to_vec(),
            vec![1., w, 1., w, 1., w, 1., w, 1.],
        )
        .unwrap()
    };
    Extrusion {
        frame: Frame::default(),
        displacement: [0., 0., 30.],
        regions: vec![Region {
            outer: vec![circle(10., 1.)],
            holes: vec![vec![circle(5., -1.)]],
        }],
    }
}
fn sub(a: [f64; 3], b: [f64; 3]) -> [f64; 3] {
    std::array::from_fn(|i| a[i] - b[i])
}
fn cross(a: [f64; 3], b: [f64; 3]) -> [f64; 3] {
    [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    ]
}
fn dot(a: [f64; 3], b: [f64; 3]) -> f64 {
    (0..3).map(|i| a[i] * b[i]).sum()
}
fn inspect(mesh: &Mesh) -> (f64, f64) {
    assert!(!mesh.vertices().is_empty());
    assert!(mesh.vertices().len() <= 3_000_000);
    assert!(!mesh.indices().is_empty());
    assert_eq!(mesh.indices().len() % 3, 0);
    assert!(mesh.indices().len() <= 3_000_000);
    let mut offset = 0;
    for (i, range) in mesh.faces().iter().enumerate() {
        assert_eq!(range.face.kind(), EntityKind::Face);
        assert_eq!(range.first_index as usize, offset);
        assert!(range.index_count > 0);
        assert_eq!(range.index_count % 3, 0);
        offset += range.index_count as usize;
        assert!(offset <= mesh.indices().len());
        for prior in &mesh.faces()[..i] {
            assert!(!prior.face.same_as(&range.face).unwrap());
        }
    }
    assert_eq!(offset, mesh.indices().len());
    for v in mesh.vertices() {
        assert!(v.position.into_iter().chain(v.normal).all(f64::is_finite));
        assert!((dot(v.normal, v.normal).sqrt() - 1.).abs() <= 1e-6);
    }
    let origin = mesh.vertices()[0].position;
    let (mut volume, mut area) = (0., 0.);
    for triangle in mesh.indices().as_chunks::<3>().0 {
        let [a, b, c] = std::array::from_fn(|i| mesh.vertices()[triangle[i] as usize]);
        let n = cross(sub(b.position, a.position), sub(c.position, a.position));
        assert!(dot(n, n) > 0.);
        assert!(
            dot(
                n,
                std::array::from_fn(|i| a.normal[i] + b.normal[i] + c.normal[i])
            ) > 0.
        );
        area += dot(n, n).sqrt() / 2.;
        volume += dot(
            sub(a.position, origin),
            cross(sub(b.position, origin), sub(c.position, origin)),
        ) / 6.;
    }
    (volume, area)
}
#[test]
fn mesh_value_layout_matches_independent_c17_and_cpp() {
    use std::mem::{align_of, offset_of, size_of};
    assert_eq!(size_of::<MeshingOptions>(), 24);
    assert_eq!(align_of::<MeshingOptions>(), 8);
    assert_eq!(offset_of!(MeshingOptions, max_triangles), 16);
    assert_eq!(size_of::<Vertex>(), 48);
    assert_eq!(align_of::<Vertex>(), 8);
    assert_eq!(offset_of!(Vertex, normal), 24);
}
#[test]
fn safe_mesh_has_analytic_box_buffers_and_original_face_picking() {
    let Some(context) = context() else { return };
    let result = context.extrude(&box_input()).unwrap();
    let options = MeshingOptions::default();
    let mesh = context.tessellate(&result.shape, &options).unwrap();
    assert_eq!(options, MeshingOptions::default());
    assert_eq!(
        (
            mesh.vertices().len(),
            mesh.indices().len(),
            mesh.faces().len()
        ),
        (24, 36, 6)
    );
    let (v, a) = inspect(&mesh);
    assert!((v - 6000.).abs() <= 1e-7);
    assert!((a - 2200.).abs() <= 1e-7);
    for range in mesh.faces() {
        assert_eq!(
            result
                .generated
                .iter()
                .filter(|g| g.entity.same_as(&range.face).unwrap())
                .count(),
            1
        );
    }
}
#[test]
fn mesh_and_independent_picking_readers_outlive_all_facades_and_mesh_clones() {
    let Some(context) = context() else { return };
    let result = context.extrude(&box_input()).unwrap();
    let mesh = context
        .tessellate(&result.shape, &MeshingOptions::default())
        .unwrap();
    let retained = mesh.clone();
    let first = mesh.faces()[0].face.clone();
    let other = mesh.faces()[1].face.clone();
    let generated = result
        .generated
        .iter()
        .find(|g| g.entity.same_as(&first).unwrap())
        .unwrap()
        .entity
        .clone();
    assert_eq!(mesh.vertices().as_ptr(), retained.vertices().as_ptr());
    assert_eq!(mesh.indices().as_ptr(), retained.indices().as_ptr());
    drop(result);
    drop(context);
    drop(mesh);
    let (v, a) = inspect(&retained);
    assert!((v - 6000.).abs() <= 1e-7);
    assert!((a - 2200.).abs() <= 1e-7);
    drop(retained);
    assert!(first.same_as(&first.clone()).unwrap());
    assert!(first.same_as(&generated).unwrap());
    assert!(!first.same_as(&other).unwrap());
}
#[test]
fn meshes_preserve_rotated_world_space_normals_and_input_measures() {
    let Some(context) = context() else { return };
    let source = context.extrude(&box_input()).unwrap();
    let result = context
        .transform(
            &source.shape,
            &RigidTransform {
                translation: [-4., 3., 7.],
                axis_origin: [0., 2., 3.],
                axis_direction: [5., 0., 0.],
                angle: std::f64::consts::FRAC_PI_2,
            },
        )
        .unwrap();
    let mesh = context
        .tessellate(&result.shape, &MeshingOptions::default())
        .unwrap();
    let (v, a) = inspect(&mesh);
    assert!((v - 6000.).abs() <= 1e-7);
    assert!((a - 2200.).abs() <= 1e-7);
    for (axis, (min, max)) in [-4., -22., 8.].into_iter().zip([6., 8., 28.]).enumerate() {
        let actual_min = mesh
            .vertices()
            .iter()
            .map(|v| v.position[axis])
            .fold(f64::INFINITY, f64::min);
        let actual_max = mesh
            .vertices()
            .iter()
            .map(|v| v.position[axis])
            .fold(f64::NEG_INFINITY, f64::max);
        assert!((actual_min - min).abs() <= 2e-7);
        assert!((actual_max - max).abs() <= 2e-7);
    }
    assert!((source.shape.measures().centroid[0] - 5.).abs() <= 1e-10);
}
#[test]
fn curved_bores_converge_without_changing_earlier_meshes_or_exact_sources() {
    let Some(context) = context() else { return };
    let source = context.extrude(&ring_input()).unwrap();
    let before = *source.shape.measures();
    let coarse = context
        .tessellate(
            &source.shape,
            &MeshingOptions {
                linear_deflection: 0.8,
                angular_deflection: 0.6,
                ..MeshingOptions::default()
            },
        )
        .unwrap();
    let saved_v = coarse.vertices().to_vec();
    let saved_i = coarse.indices().to_vec();
    let fine = context
        .tessellate(
            &source.shape,
            &MeshingOptions {
                linear_deflection: 0.02,
                angular_deflection: 0.15,
                ..MeshingOptions::default()
            },
        )
        .unwrap();
    let (cv, _) = inspect(&coarse);
    let (fv, fa) = inspect(&fine);
    let (v, a) = (2250. * std::f64::consts::PI, 1050. * std::f64::consts::PI);
    assert!(fine.indices().len() > coarse.indices().len());
    assert!((fv - v).abs() < (cv - v).abs());
    assert!((fv - v).abs() <= v * 0.002);
    assert!((fa - a).abs() <= a * 0.002);
    assert_eq!(coarse.vertices(), saved_v);
    assert_eq!(coarse.indices(), saved_i);
    assert_eq!(*source.shape.measures(), before);
    let mut bore = 0;
    for p in fine.vertices() {
        if (p.position[0].hypot(p.position[1]) - 5.).abs() <= 1e-6 && p.normal[2].abs() <= 1e-6 {
            assert!(
                ((p.position[0] * p.normal[0] + p.position[1] * p.normal[1]) / 5. + 1.).abs()
                    <= 1e-6
            );
            bore += 1;
        }
    }
    assert!(bore > 0);
}
#[test]
fn foreign_mesh_contexts_invalid_deflections_and_budgets_recover_without_input_changes() {
    let Some(context) = context() else { return };
    let source = context.extrude(&box_input()).unwrap();
    let before = *source.shape.measures();
    let foreign = Context::new(1e-6, 1e-9).unwrap();
    assert!(matches!(
        foreign.tessellate(&source.shape, &MeshingOptions::default()),
        Err(NativeError::InvalidArgument)
    ));
    for bad in [0., -1., 1e-10, f64::NAN, f64::INFINITY, f64::NEG_INFINITY] {
        for options in [
            MeshingOptions {
                linear_deflection: bad,
                ..MeshingOptions::default()
            },
            MeshingOptions {
                angular_deflection: bad,
                ..MeshingOptions::default()
            },
        ] {
            assert!(matches!(
                context.tessellate(&source.shape, &options),
                Err(NativeError::Geometry { .. })
            ));
        }
    }
    for count in [0, 11, 1_000_001, u64::MAX] {
        assert!(matches!(
            context.tessellate(
                &source.shape,
                &MeshingOptions {
                    max_triangles: count,
                    ..MeshingOptions::default()
                }
            ),
            Err(NativeError::Geometry { .. })
        ));
    }
    let mesh = context
        .tessellate(
            &source.shape,
            &MeshingOptions {
                max_triangles: 12,
                ..MeshingOptions::default()
            },
        )
        .unwrap();
    assert_eq!(mesh.indices().len(), 36);
    assert_eq!(*source.shape.measures(), before);
}
#[test]
fn retained_meshes_and_picking_faces_are_neither_send_nor_sync() {
    macro_rules! local {
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
    local!(Mesh);
    local!(MeshFace);
    local!(Entity);
}
