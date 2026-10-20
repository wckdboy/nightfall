// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use super::*;
#[test]
fn step_metadata_rejects_empty_null_and_overflow_counts_before_borrowing() {
    let bytes = [42u8; 2];
    for info in [
        ffi::Info::default(),
        ffi::Info {
            bytes: std::ptr::null(),
            byte_count: 1,
        },
        ffi::Info {
            bytes: bytes.as_ptr(),
            byte_count: 0,
        },
        ffi::Info {
            bytes: bytes.as_ptr(),
            byte_count: isize::MAX as u64 + 1,
        },
        ffi::Info {
            bytes: bytes.as_ptr(),
            byte_count: u64::MAX,
        },
    ] {
        assert_eq!(check_info(&info), Err(NativeError::AbiMismatch));
    }
    assert_eq!(
        check_info(&ffi::Info {
            bytes: bytes.as_ptr(),
            byte_count: 2
        }),
        Ok(2)
    );
}
#[test]
fn retained_step_is_neither_send_nor_sync() {
    trait AmbiguousIfSend<A> {
        fn check() {}
    }
    impl<T: ?Sized> AmbiguousIfSend<()> for T {}
    impl<T: ?Sized + Send> AmbiguousIfSend<u8> for T {}
    let _ = <Step as AmbiguousIfSend<_>>::check;
    trait AmbiguousIfSync<A> {
        fn check() {}
    }
    impl<T: ?Sized> AmbiguousIfSync<()> for T {}
    impl<T: ?Sized + Sync> AmbiguousIfSync<u8> for T {}
    let _ = <Step as AmbiguousIfSync<_>>::check;
}
