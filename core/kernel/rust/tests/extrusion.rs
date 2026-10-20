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
fn line(a: [f64; 2], b: [f64; 2]) -> NurbsCurve2 {
    NurbsCurve2::new(
        KnotVector::new(1, vec![0., 0., 1., 1.]).unwrap(),
        vec![a, b],
        vec![],
    )
    .unwrap()
}
fn rectangle() -> Vec<NurbsCurve2> {
    vec![
        line([0., 0.], [10., 0.]),
        line([10., 0.], [10., 20.]),
        line([10., 20.], [0., 20.]),
        line([0., 20.], [0., 0.]),
    ]
}
fn input() -> Extrusion {
    Extrusion {
        frame: Frame::default(),
        displacement: [0., 0., 30.],
        regions: vec![Region {
            outer: rectangle(),
            holes: vec![],
        }],
    }
}
fn circle(radius: f64, clockwise: bool) -> NurbsCurve2 {
    let sign = if clockwise { -1. } else { 1. };
    let points = [
        [1., 0.],
        [1., 1.],
        [0., 1.],
        [-1., 1.],
        [-1., 0.],
        [-1., -1.],
        [0., -1.],
        [1., -1.],
        [1., 0.],
    ];
    let w = std::f64::consts::FRAC_1_SQRT_2;
    NurbsCurve2::new(
        KnotVector::new(2, vec![0., 0., 0., 1., 1., 2., 2., 3., 3., 4., 4., 4.]).unwrap(),
        points
            .map(|p| [radius * p[0], radius * p[1] * sign])
            .to_vec(),
        vec![1., w, 1., w, 1., w, 1., w, 1.],
    )
    .unwrap()
}
fn failure(at: InputLocation, problem: InputProblem) -> KernelError {
    KernelError::Input { at, problem }
}
fn assert_failure(kernel: &Kernel, input: &Extrusion, expected: KernelError) {
    assert_eq!(kernel.validate_extrusion(input), Err(expected.clone()));
    assert!(matches!(kernel.extrude(input), Err(error) if error == expected));
}
#[test]
fn typed_box_and_retained_entities_are_independent_of_the_facade() {
    let Some(kernel) = kernel() else { return };
    let input = input();
    let result = kernel.extrude(&input).unwrap();
    assert!(result.history.is_empty());
    assert!(result.estimated_bytes > 0);
    assert_eq!(result.generated.len(), 6);
    assert_eq!(result.generated[0].role, "cap_start");
    assert_eq!(result.generated[1].role, "cap_end");
    assert_eq!(
        result.generated[2].from,
        Some(ProfileEdge {
            region: 0,
            loop_index: 0,
            curve: 0,
            section: 0
        })
    );
    let shape = result.shape.clone();
    let cap = result.generated[0].entity.clone();
    let side = result.generated[2].entity.clone();
    drop(result);
    drop(input);
    drop(kernel);
    let m = shape.measures();
    assert!((m.volume - 6000.).abs() <= 1e-8);
    assert!((m.area - 2200.).abs() <= 1e-8);
    assert_eq!((m.solids, m.faces, m.edges), (1, 6, 12));
    for (actual, expected) in m.centroid.into_iter().zip([5., 10., 15.]) {
        assert!((actual - expected).abs() <= 1e-8);
    }
    drop(shape);
    assert_eq!(side.kind(), EntityKind::Face);
    assert!(side.same_as(&side.clone()).unwrap());
    assert!(!side.same_as(&cap).unwrap());
}
#[test]
fn exact_hole_and_placed_oblique_goldens_preserve_inputs() {
    let Some(kernel) = kernel() else { return };
    let annulus = Extrusion {
        frame: Frame::default(),
        displacement: [0., 0., 5.],
        regions: vec![Region {
            outer: vec![circle(10., false)],
            holes: vec![vec![circle(4., true)]],
        }],
    };
    let points = annulus.regions[0].holes[0][0].points().to_vec();
    let result = kernel.extrude(&annulus).unwrap();
    let m = result.shape.measures();
    assert!((m.volume - 420. * std::f64::consts::PI).abs() <= 1e-5);
    assert!((m.area - 308. * std::f64::consts::PI).abs() <= 1e-5);
    assert_eq!(annulus.regions[0].holes[0][0].points(), points);
    for placed in [false, true] {
        let mut input = input();
        let expected = if placed {
            input.frame = Frame {
                origin: [7., 2., 3.],
                x_axis: [0., 1., 0.],
                normal: [1., 0., 0.],
            };
            input.displacement = [-30., 0., 0.];
            [-8., 7., 13.]
        } else {
            input.displacement = [4., -7., 30.];
            [7., 6.5, 15.]
        };
        let result = kernel.extrude(&input).unwrap();
        assert!((result.shape.measures().volume - 6000.).abs() <= 1e-8);
        for (actual, expected) in result.shape.measures().centroid.into_iter().zip(expected) {
            assert!((actual - expected).abs() <= 1e-8);
        }
    }
}
#[test]
fn finite_input_errors_identify_the_exact_field_coordinate() {
    let Some(kernel) = kernel() else { return };
    for value in [f64::NAN, f64::INFINITY, f64::NEG_INFINITY] {
        for axis in 0..3 {
            for field in 0..4 {
                let mut input = input();
                let at = match field {
                    0 => {
                        input.frame.origin[axis] = value;
                        InputLocation::FrameOrigin(axis)
                    }
                    1 => {
                        input.frame.x_axis[axis] = value;
                        InputLocation::FrameXAxis(axis)
                    }
                    2 => {
                        input.frame.normal[axis] = value;
                        InputLocation::FrameNormal(axis)
                    }
                    _ => {
                        input.displacement[axis] = value;
                        InputLocation::Displacement(axis)
                    }
                };
                assert_failure(&kernel, &input, failure(at, InputProblem::NonFinite));
            }
        }
    }
}
#[test]
fn empty_and_disconnected_profiles_identify_region_loop_and_curve() {
    let Some(kernel) = kernel() else { return };
    let mut empty = input();
    empty.regions.clear();
    assert_failure(
        &kernel,
        &empty,
        failure(InputLocation::Regions, InputProblem::Empty),
    );
    for loop_index in 0..2 {
        let mut empty = input();
        empty.regions.push(Region {
            outer: rectangle(),
            holes: vec![],
        });
        if loop_index == 0 {
            empty.regions[1].outer.clear();
        } else {
            empty.regions[1].holes.push(vec![]);
        }
        assert_failure(
            &kernel,
            &empty,
            failure(
                InputLocation::Loop {
                    region: 1,
                    loop_index,
                },
                InputProblem::Empty,
            ),
        );
        let mut disconnected = input();
        if loop_index == 0 {
            disconnected.regions[0].outer.pop();
        } else {
            disconnected.regions[0]
                .holes
                .push(vec![line([1., 1.], [2., 1.])]);
        }
        assert_failure(
            &kernel,
            &disconnected,
            failure(
                InputLocation::Curve {
                    region: 0,
                    loop_index,
                    curve: if loop_index == 0 { 2 } else { 0 },
                },
                InputProblem::Disconnected,
            ),
        );
    }
}
#[test]
fn endpoint_connectivity_uses_explicit_euclidean_mm_tolerance() {
    let Some(kernel) = kernel() else { return };
    for (offset, accepted) in [
        ([1e-6, 0.], true),
        ([0.6e-6, 0.6e-6], true),
        ([0.8e-6, 0.8e-6], false),
    ] {
        let mut input = input();
        input.regions[0].outer[3] = line([0., 20.], offset);
        if accepted {
            assert_eq!(kernel.validate_extrusion(&input), Ok(()));
        } else {
            assert_failure(
                &kernel,
                &input,
                failure(
                    InputLocation::Curve {
                        region: 0,
                        loop_index: 0,
                        curve: 3,
                    },
                    InputProblem::Disconnected,
                ),
            );
        }
    }
    let looser = Kernel::new(2e-6, 1e-9).unwrap();
    let mut input = input();
    input.regions[0].outer[3] = line([0., 20.], [1.5e-6, 0.]);
    assert!(looser.validate_extrusion(&input).is_ok());
    assert_failure(
        &kernel,
        &input,
        failure(
            InputLocation::Curve {
                region: 0,
                loop_index: 0,
                curve: 3,
            },
            InputProblem::Disconnected,
        ),
    );
}
#[test]
fn endpoint_evaluation_failures_keep_curve_source_locations() {
    let Some(kernel) = kernel() else { return };
    let extreme = NurbsCurve2::new(
        KnotVector::new(1, vec![-f64::MAX, -f64::MAX, f64::MAX, f64::MAX]).unwrap(),
        vec![[0., 0.], [1., 0.]],
        vec![],
    )
    .unwrap();
    assert_eq!(extreme.point(f64::MAX), Err(MathError::NumericalRange));
    let mut input = input();
    input.regions[0].holes.push(vec![extreme]);
    assert_failure(
        &kernel,
        &input,
        failure(
            InputLocation::Curve {
                region: 0,
                loop_index: 1,
                curve: 0,
            },
            InputProblem::Evaluation(MathError::NumericalRange),
        ),
    );
}
#[test]
fn native_geometry_rejection_publishes_nothing_and_does_not_damage_snapshots() {
    let Some(kernel) = kernel() else { return };
    let original = kernel.extrude(&input()).unwrap();
    for case in 0..5 {
        let mut input = input();
        match case {
            0 => input.displacement = [0.; 3],
            1 => input.displacement = [3., 4., 0.],
            2 => input.frame.normal = [0., 0., 2.],
            3 => input.frame.x_axis = [0., 0., 1.],
            _ => {
                input.regions[0].outer = vec![line([0., 0.], [1., 0.]), line([1., 0.], [0., 0.])];
            }
        }
        assert_eq!(kernel.validate_extrusion(&input), Ok(()));
        assert!(matches!(
            kernel.extrude(&input),
            Err(KernelError::Native(NativeError::Geometry { .. }))
        ));
    }
    assert!((original.shape.measures().volume - 6000.).abs() <= 1e-8);
    assert!((kernel.extrude(&input()).unwrap().shape.measures().volume - 6000.).abs() <= 1e-8);
}
#[test]
fn entity_owners_reject_foreign_kernels_and_distinct_snapshots_compare_unequal() {
    let Some(first) = kernel() else { return };
    let second = Kernel::new(1e-6, 1e-9).unwrap();
    let a = first.extrude(&input()).unwrap();
    let b = first.extrude(&input()).unwrap();
    let c = second.extrude(&input()).unwrap();
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
fn implementation_receipts_are_namespaced_and_include_backend_configuration() {
    let Some(kernel) = kernel() else { return };
    let info = kernel.info().unwrap();
    assert!(info.identity.starts_with(
        "nightfall-rust/0.0.1;policy=extrusion-rigid-mesh-step-boolean-v1;native=occt/7.9.3;"
    ));
    assert!(info.identity.ends_with(&info.backend.identity));
    assert_ne!(info.identity, info.backend.identity);
    assert_ne!(
        info.identity,
        Kernel::new(2e-6, 2e-9).unwrap().info().unwrap().identity
    );
    drop(kernel);
    std::thread::spawn(move || {
        assert!(info.identity.contains(&info.backend.identity));
    })
    .join()
    .unwrap();
}
#[test]
fn invalid_tolerances_and_unavailable_backend_are_typed_failures() {
    for value in [0., -1., f64::NAN, f64::INFINITY, f64::NEG_INFINITY] {
        assert!(matches!(
            Kernel::new(value, 1e-9),
            Err(KernelError::Native(NativeError::InvalidArgument))
        ));
        assert!(matches!(
            Kernel::new(1e-6, value),
            Err(KernelError::Native(NativeError::InvalidArgument))
        ));
    }
    assert!(matches!(
        Kernel::new(1e-6, 1.),
        Err(KernelError::Native(NativeError::InvalidArgument))
    ));
    if let Some(kernel) = kernel() {
        drop(kernel);
    }
}
#[test]
fn kernel_and_results_are_neither_send_nor_sync() {
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
    assert_local!(Kernel);
    assert_local!(Shape);
    assert_local!(Entity);
    assert_local!(OperationResult);
}
