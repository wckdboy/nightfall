// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use crate::{boolean::BooleanOp, boolean_ffi as ffi};
#[test]
fn rust_boolean_signature_and_modes_reject_null_and_misaligned_outputs() {
    assert_eq!(std::mem::size_of::<BooleanOp>(), 4);
    assert_eq!(
        (
            BooleanOp::Union as u32,
            BooleanOp::Subtract as u32,
            BooleanOp::Intersect as u32
        ),
        (0, 1, 2)
    );
    let mut out = std::ptr::null_mut();
    // SAFETY: null inputs are rejected before access; output slot is aligned and writable.
    assert_eq!(
        unsafe {
            ffi::nf_native_boolean(
                std::ptr::null_mut(),
                std::ptr::null(),
                std::ptr::null(),
                0,
                0,
                &mut out,
            )
        },
        1
    );
    assert!(out.is_null());
    #[repr(align(8))]
    struct Slot([u8; 9]);
    let mut slot = Slot([0x7f; 9]);
    // SAFETY: intentionally misaligned live output storage must be rejected without a write.
    assert_eq!(
        unsafe {
            ffi::nf_native_boolean(
                std::ptr::null_mut(),
                std::ptr::null(),
                std::ptr::null(),
                0,
                0,
                slot.0.as_mut_ptr().wrapping_add(1).cast(),
            )
        },
        1
    );
    assert_eq!(slot.0, [0x7f; 9]);
}
