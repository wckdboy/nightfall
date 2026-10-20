// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use crate::extrusion::{Extrusion, Frame, Region};
use crate::{Context, NativeError, c_void, with_native};
use nightfall_math::{KnotVector, NurbsCurve2};
#[repr(C)]
#[derive(Default)]
struct StepInfo {
    bytes: *const u8,
    byte_count: u64,
}
unsafe extern "C" {
    fn nf_native_export_step(
        context: *mut c_void,
        sources: *const *const c_void,
        source_count: u64,
        out: *mut *mut c_void,
    ) -> u32;
    fn nf_native_step_query(step: *const c_void, out: *mut StepInfo) -> u32;
    fn nf_native_step_release(step: *mut c_void);
}
#[test]
fn step_byte_record_matches_independent_c17_and_cpp_layouts() {
    use std::mem::{align_of, offset_of, size_of};
    assert_eq!(size_of::<StepInfo>(), 16);
    assert_eq!(align_of::<StepInfo>(), 8);
    assert_eq!(offset_of!(StepInfo, byte_count), 8);
}
#[test]
fn step_abi_null_inputs_clear_valid_outputs_without_native_owners() {
    with_native(|| {
        let mut out = std::ptr::null_mut();
        // SAFETY: null inputs and an aligned writable output; no owner is published.
        assert_eq!(
            unsafe { nf_native_export_step(std::ptr::null_mut(), std::ptr::null(), 0, &mut out) },
            1
        );
        assert!(out.is_null());
        let mut info = StepInfo {
            byte_count: 42,
            ..StepInfo::default()
        };
        // SAFETY: null owner and aligned writable record, cleared on failure.
        assert_eq!(
            unsafe { nf_native_step_query(std::ptr::null(), &mut info) },
            1
        );
        assert!(info.bytes.is_null());
        assert_eq!(info.byte_count, 0);
        // SAFETY: null release is explicitly allowed.
        unsafe { nf_native_step_release(std::ptr::null_mut()) };
    });
}
struct StepOwner(*mut c_void);
impl Drop for StepOwner {
    fn drop(&mut self) {
        // SAFETY: this test owner releases one allocation in its allocating module.
        with_native(|| unsafe { nf_native_step_release(self.0) });
    }
}
#[test]
fn rust_abi_reads_retained_step_bytes_after_all_geometry_owners_end() {
    let context = Context::new(1e-6, 1e-9);
    if std::env::var("NF_NATIVE_EXPECT_OCCT").expect("run via CTest") == "0" {
        assert!(matches!(context, Err(NativeError::Unavailable)));
        return;
    }
    let context = context.unwrap();
    let points = [[0., 0.], [10., 0.], [10., 20.], [0., 20.], [0., 0.]];
    let input = Extrusion {
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
    };
    let source = context.extrude(&input).unwrap();
    let pointer = source
        .shape
        .native_pointer(&context)
        .unwrap()
        .as_ptr()
        .cast_const();
    let sources = [pointer, pointer];
    let (status, pointer) = with_native(|| {
        let mut out = std::ptr::null_mut();
        // SAFETY: retained same-context immutable sources, live aligned range/output.
        let status =
            unsafe { nf_native_export_step(context.0.0.as_ptr(), sources.as_ptr(), 2, &mut out) };
        (status, out)
    });
    let retained = StepOwner(pointer);
    assert_eq!(status, 0);
    assert!(!retained.0.is_null());
    drop(input);
    drop(source);
    drop(context);
    with_native(|| {
        let mut info = StepInfo::default();
        // SAFETY: retained independent owner and writable aligned output.
        assert_eq!(unsafe { nf_native_step_query(retained.0, &mut info) }, 0);
        assert!(!info.bytes.is_null());
        assert!(info.byte_count > 0 && info.byte_count <= isize::MAX as u64);
        // SAFETY: live immutable native allocation, bounded count, retained throughout this borrow.
        let bytes = unsafe { std::slice::from_raw_parts(info.bytes, info.byte_count as usize) };
        let text = std::str::from_utf8(bytes).unwrap();
        assert!(text.starts_with("ISO-10303-21;"));
        assert!(text.contains("AP242"));
        assert!(!text.contains("TESSELLATED"));
        let mut again = StepInfo::default();
        // SAFETY: same retained owner, independently writable record.
        assert_eq!(unsafe { nf_native_step_query(retained.0, &mut again) }, 0);
        assert_eq!(again.bytes, info.bytes);
        assert_eq!(again.byte_count, info.byte_count);
    });
}
