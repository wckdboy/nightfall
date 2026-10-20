// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use nightfall_native::{Context, NativeError, Operation};
fn available() -> bool {
    std::env::var("NF_NATIVE_EXPECT_OCCT").expect("run via CTest") == "1"
}

#[test]
fn invalid_tolerances_fail_without_an_owner() {
    for value in [0.0, -1.0, f64::NAN, f64::INFINITY, f64::NEG_INFINITY] {
        assert!(matches!(
            Context::new(value, 1e-9),
            Err(NativeError::InvalidArgument)
        ));
        assert!(matches!(
            Context::new(1e-6, value),
            Err(NativeError::InvalidArgument)
        ));
    }
    assert!(matches!(
        Context::new(1e-6, 1.0),
        Err(NativeError::InvalidArgument)
    ));
    if available() {
        assert!(matches!(
            Context::new(1e-100, 1e-9),
            Err(NativeError::InvalidArgument)
        ));
        assert!(matches!(
            Context::new(1e-6, 1e-100),
            Err(NativeError::InvalidArgument)
        ));
    }
}
#[test]
fn retained_context_survives_facade_drop_and_independent_contexts() {
    let context = Context::new(1e-6, 1e-9);
    if !available() {
        assert!(matches!(context, Err(NativeError::Unavailable)));
        return;
    }
    let context = context.unwrap();
    let identity = context.info().unwrap().identity;
    assert!(identity.starts_with("occt/7.9.3;build="));
    let retained = context.clone();
    drop(context);
    for _ in 0..128 {
        drop(Context::new(1e-6, 1e-9).unwrap());
        assert_eq!(retained.info().unwrap().identity, identity);
    }
    let info = retained.info().unwrap();
    for operation in [
        Operation::Extrude,
        Operation::RigidTransform,
        Operation::Booleans,
        Operation::Revolve,
        Operation::Mirror,
        Operation::Chamfer,
        Operation::Fillet,
        Operation::Loft,
        Operation::Sweep,
    ] {
        assert!(info.capabilities.supports(operation));
    }
    let different = Context::new(2e-6, 2e-9).unwrap();
    assert_ne!(different.info().unwrap().identity, identity);
    assert_eq!(retained.info().unwrap().identity, identity);
    drop(different);
    drop(retained);
    std::thread::spawn(move || {
        assert_eq!(info.identity, identity);
        assert!(info.capabilities.supports(Operation::Extrude));
    })
    .join()
    .unwrap();
}
#[test]
fn independent_threads_create_query_and_release_their_own_contexts() {
    let expected = available();
    let workers: Vec<_> = (0..8)
        .map(|_| {
            std::thread::spawn(move || {
                for _ in 0..128 {
                    match Context::new(1e-6, 1e-9) {
                        Ok(context) => {
                            assert!(expected);
                            assert!(
                                context
                                    .info()
                                    .unwrap()
                                    .capabilities
                                    .supports(Operation::Extrude)
                            );
                        }
                        Err(error) => {
                            assert!(!expected);
                            assert_eq!(error, NativeError::Unavailable);
                        }
                    }
                }
            })
        })
        .collect();
    for worker in workers {
        worker.join().unwrap();
    }
}
