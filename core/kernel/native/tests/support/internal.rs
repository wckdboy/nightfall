// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use super::*;
#[test]
fn context_cannot_implement_send_or_sync() {
    // One implementation is inferable only while Context lacks the corresponding auto trait.
    // This positive compilation check cannot pass because a compile-fail example fails to link.
    trait AmbiguousIfSend<A> {
        fn check() {}
    }
    impl<T: ?Sized> AmbiguousIfSend<()> for T {}
    impl<T: ?Sized + Send> AmbiguousIfSend<u8> for T {}
    let _ = <Context as AmbiguousIfSend<_>>::check;
    trait AmbiguousIfSync<A> {
        fn check() {}
    }
    impl<T: ?Sized> AmbiguousIfSync<()> for T {}
    impl<T: ?Sized + Sync> AmbiguousIfSync<u8> for T {}
    let _ = <Context as AmbiguousIfSync<_>>::check;
}
#[test]
fn ffi_layout_and_all_status_values_match_the_c_header() {
    assert_eq!(
        std::mem::size_of::<RawInfo>(),
        8 + std::mem::size_of::<usize>() * 2
    );
    assert_eq!(
        std::mem::align_of::<RawInfo>(),
        std::mem::align_of::<usize>()
    );
    assert_eq!(std::mem::offset_of!(RawInfo, identity), 8);
    assert_eq!(
        std::mem::offset_of!(RawInfo, identity_bytes),
        8 + std::mem::size_of::<usize>()
    );
    assert_eq!(status(0), Ok(()));
    assert_eq!(status(1), Err(NativeError::InvalidArgument));
    assert_eq!(status(2), Err(NativeError::Unavailable));
    assert_eq!(status(3), Err(NativeError::NativeFailure));
    assert_eq!(status(4), Err(NativeError::AbiMismatch));
    assert_eq!(status(u32::MAX), Err(NativeError::AbiMismatch));
}
#[test]
fn guard_serializes_threads_and_recovers_poison_for_release() {
    use std::sync::{
        Arc,
        atomic::{AtomicUsize, Ordering},
    };
    let active = Arc::new(AtomicUsize::new(0));
    let workers: Vec<_> = (0..8)
        .map(|_| {
            let active = active.clone();
            std::thread::spawn(move || {
                for _ in 0..256 {
                    with_native(|| {
                        assert_eq!(active.fetch_add(1, Ordering::SeqCst), 0);
                        std::thread::yield_now();
                        assert_eq!(active.fetch_sub(1, Ordering::SeqCst), 1);
                    });
                }
            })
        })
        .collect();
    for worker in workers {
        worker.join().unwrap();
    }
    assert!(std::panic::catch_unwind(|| with_native(|| panic!("test-only poison"))).is_err());
    with_native(|| assert_eq!(active.load(Ordering::SeqCst), 0));
    // Release must remain usable after poison as well as metadata/create.
    if let Ok(context) = Context::new(1e-6, 1e-9) {
        assert!(context.info().is_ok());
        drop(context);
    }
}
