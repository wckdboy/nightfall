// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use super::*;
use crate::extrusion::{Frame, Measures};
use crate::extrusion_ffi::*;
struct Owned(*mut c_void);
impl Drop for Owned {
    fn drop(&mut self) {
        // SAFETY: this test owner releases its single successful allocation exactly once.
        with_native(|| unsafe { nf_native_result_release(self.0) });
    }
}
fn context() -> Option<Context> {
    let value = Context::new(1e-6, 1e-9);
    if std::env::var("NF_NATIVE_EXPECT_OCCT").unwrap() == "0" {
        assert!(matches!(value, Err(NativeError::Unavailable)));
        None
    } else {
        Some(value.unwrap())
    }
}
fn make_box(context: &Context) -> Owned {
    // All profile buffers die before returning; the result must own exact geometry.
    let knots = [0.0, 0.0, 1.0, 1.0];
    let points = [
        [Point { x: 0.0, y: 0.0 }, Point { x: 10.0, y: 0.0 }],
        [Point { x: 10.0, y: 0.0 }, Point { x: 10.0, y: 20.0 }],
        [Point { x: 10.0, y: 20.0 }, Point { x: 0.0, y: 20.0 }],
        [Point { x: 0.0, y: 20.0 }, Point { x: 0.0, y: 0.0 }],
    ];
    let curves: [_; 4] = std::array::from_fn(|i| Curve {
        degree: 1,
        knots: knots.as_ptr(),
        knot_count: 4,
        points: points[i].as_ptr(),
        point_count: 2,
        weights: std::ptr::null(),
        weight_count: 0,
    });
    let region = Region {
        outer: Loop {
            curves: curves.as_ptr(),
            count: 4,
        },
        holes: std::ptr::null(),
        hole_count: 0,
    };
    let input = Extrusion {
        frame: Frame::default(),
        displacement: [0.0, 0.0, 30.0],
        regions: &region,
        region_count: 1,
    };
    let mut output = std::ptr::null_mut();
    // SAFETY: aligned live borrowed ranges, retained context and writable output.
    assert_eq!(
        with_native(|| unsafe { nf_native_extrude(context.0.0.as_ptr(), &input, &mut output) }),
        0
    );
    assert!(!output.is_null());
    Owned(output)
}
fn info(result: &Owned) -> Info {
    let mut output = Info::default();
    // SAFETY: retained immutable result and valid output slot.
    assert_eq!(
        with_native(|| unsafe { nf_native_result_query(result.0, &mut output) }),
        0
    );
    output
}
#[test]
fn extrusion_record_layout_and_null_failures_match_c() {
    assert_eq!(std::mem::size_of::<Curve>(), 56);
    assert_eq!(std::mem::size_of::<Info>(), 136);
    assert_eq!(std::mem::size_of::<Generated>(), 64);
    assert_eq!(std::mem::offset_of!(Curve, knots), 8);
    assert_eq!(
        std::mem::offset_of!(Info, measures) + std::mem::offset_of!(Measures, solids),
        88
    );
    assert_eq!(std::mem::offset_of!(Generated, has_from), 56);
    let mut result = std::ptr::null_mut();
    // SAFETY: null inputs are documented failures; output is a writable slot.
    assert_eq!(
        with_native(|| unsafe {
            nf_native_extrude(std::ptr::null_mut(), std::ptr::null(), &mut result)
        }),
        1
    );
    assert!(result.is_null());
    let mut output = Info {
        measures: Measures {
            volume: 42.0,
            ..Measures::default()
        },
        ..Info::default()
    };
    // SAFETY: null result is a documented error and output is live/aligned.
    assert_eq!(
        with_native(|| unsafe { nf_native_result_query(std::ptr::null(), &mut output) }),
        1
    );
    assert_eq!(output.measures.volume, 0.0);
}
#[test]
fn rust_abi_builds_exact_solid_after_profile_and_context_teardown() {
    let Some(context) = context() else {
        return;
    };
    let result = make_box(&context);
    drop(context);
    let output = info(&result);
    assert!((output.measures.volume - 6000.0).abs() <= 1e-8);
    assert!((output.measures.area - 2200.0).abs() <= 1e-8);
    for (actual, expected) in output.measures.centroid.into_iter().zip([5.0, 10.0, 15.0]) {
        assert!((actual - expected).abs() <= 1e-10);
    }
    assert_eq!(
        (
            output.measures.solids,
            output.measures.faces,
            output.measures.edges
        ),
        (1, 6, 12)
    );
    assert_eq!((output.generated_count, output.history_count), (6, 0));
    assert!(output.estimated_bytes > 0);
}
#[test]
fn rust_abi_retains_face_sources_and_distinguishes_snapshots() {
    let Some(context) = context() else {
        return;
    };
    let first = make_box(&context);
    let second = make_box(&context);
    drop(context);
    for index in 0..6 {
        let mut face = Generated::default();
        let mut other = Generated::default();
        // SAFETY: both results are retained; index is bounded and outputs are writable.
        with_native(|| unsafe {
            assert_eq!(nf_native_result_generated(first.0, index, &mut face), 0);
            assert_eq!(nf_native_result_generated(second.0, index, &mut other), 0);
        });
        assert!(!face.entity.is_null());
        assert_eq!(face.kind, 1);
        assert_eq!(face.has_from, u32::from(index >= 2));
        // SAFETY: this borrowed role remains live while first is retained.
        let role = unsafe { std::slice::from_raw_parts(face.role, face.role_bytes) };
        assert_eq!(
            role,
            if index == 0 {
                b"cap_start".as_slice()
            } else if index == 1 {
                b"cap_end".as_slice()
            } else {
                b"side".as_slice()
            }
        );
        if index >= 2 {
            assert_eq!(
                (face.region, face.r#loop, face.curve, face.section),
                (0, 0, index - 2, 0)
            );
        }
        let mut same = 42;
        // SAFETY: both borrowed entities have retained parents; no stale/forged pointers.
        with_native(|| unsafe {
            assert_eq!(
                nf_native_entities_same(face.entity, face.entity, &mut same),
                0
            );
            assert_eq!(same, 1);
            assert_eq!(
                nf_native_entities_same(face.entity, other.entity, &mut same),
                0
            );
            assert_eq!(same, 0);
        });
    }
}
#[test]
fn rigid_history_layout_matches_independent_c17_and_cpp_records() {
    use crate::transform::RigidTransform;
    use std::mem::{align_of, offset_of, size_of};
    assert_eq!(size_of::<RigidTransform>(), 80);
    assert_eq!(align_of::<RigidTransform>(), 8);
    assert_eq!(offset_of!(RigidTransform, angle), 72);
    assert_eq!(size_of::<crate::extrusion_ffi::History>(), 32);
    assert_eq!(offset_of!(crate::extrusion_ffi::History, before_kind), 24);
    assert_eq!(offset_of!(crate::extrusion_ffi::History, deleted), 28);
    assert_eq!(size_of::<crate::extrusion_ffi::EntityView>(), 16);
    assert_eq!(offset_of!(crate::extrusion_ffi::EntityView, kind), 8);
}
