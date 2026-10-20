// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use super::*;
#[test]
fn borrowed_buffer_and_face_records_match_independent_c17_and_cpp() {
    use std::mem::{align_of, offset_of, size_of};
    assert_eq!(size_of::<ffi::Info>(), 40);
    assert_eq!(align_of::<ffi::Info>(), 8);
    assert_eq!(offset_of!(ffi::Info, indices), 8);
    assert_eq!(offset_of!(ffi::Info, vertex_count), 16);
    assert_eq!(offset_of!(ffi::Info, face_count), 32);
    assert_eq!(size_of::<ffi::Face>(), 16);
    assert_eq!(align_of::<ffi::Face>(), 8);
    assert_eq!(offset_of!(ffi::Face, first_index), 8);
    assert_eq!(offset_of!(ffi::Face, index_count), 12);
}
#[test]
fn malformed_buffer_metadata_is_rejected_before_any_unsafe_slice_borrow() {
    for count in [0, 3_000_001, u64::MAX] {
        // Only metadata is inspected; these pointers are never dereferenced.
        assert_eq!(
            check_buffer(std::ptr::dangling::<Vertex>(), count),
            Err(NativeError::AbiMismatch)
        );
        assert_eq!(
            check_buffer(std::ptr::dangling::<u32>(), count),
            Err(NativeError::AbiMismatch)
        );
    }
    assert_eq!(
        check_buffer::<Vertex>(std::ptr::null(), 1),
        Err(NativeError::AbiMismatch)
    );
    assert_eq!(
        check_buffer::<Vertex>(std::ptr::without_provenance(1), 1),
        Err(NativeError::AbiMismatch)
    );
    assert_eq!(
        check_buffer(std::ptr::dangling::<Vertex>(), 3_000_000),
        Ok(())
    );
}
