// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#![deny(unsafe_op_in_unsafe_fn)]
use nightfall_math::{KnotVector, NurbsCurve3, basis_derivatives, basis_functions};
use std::alloc::{GlobalAlloc, Layout, System};
use std::cell::Cell;
use std::hint::black_box;

thread_local! {
    static TRACK: Cell<bool> = const { Cell::new(false) };
    static CALLS: Cell<usize> = const { Cell::new(0) };
}
struct CountingAllocator;
fn record() {
    if TRACK.try_with(Cell::get).unwrap_or(false) {
        CALLS.with(|n| n.set(n.get() + 1));
    }
}
// SAFETY: this test-only observer forwards the original layouts/pointers unchanged to
// System. Const TLS counters do not allocate; other test-harness threads are not counted.
unsafe impl GlobalAlloc for CountingAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        record();
        unsafe { System.alloc(layout) }
    }
    unsafe fn alloc_zeroed(&self, layout: Layout) -> *mut u8 {
        record();
        unsafe { System.alloc_zeroed(layout) }
    }
    unsafe fn realloc(&self, ptr: *mut u8, layout: Layout, size: usize) -> *mut u8 {
        record();
        unsafe { System.realloc(ptr, layout, size) }
    }
    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        unsafe { System.dealloc(ptr, layout) }
    }
}
#[global_allocator]
static ALLOCATOR: CountingAllocator = CountingAllocator;

#[test]
fn evaluation_and_errors_allocate_nothing() {
    // Validate the observer itself before measuring the numerical code.
    TRACK.with(|v| v.set(true));
    let observation = black_box(vec![0_u8; black_box(16)]);
    TRACK.with(|v| v.set(false));
    assert!(CALLS.with(Cell::get) > 0);
    drop(observation);
    for degree in 1..=9 {
        let mut values = vec![0.0; degree + 1];
        values.extend(std::iter::repeat_n(0.5, degree));
        values.extend(std::iter::repeat_n(1.0, degree + 1));
        let knots = KnotVector::new(degree, values).unwrap();
        CALLS.with(|v| v.set(0));
        TRACK.with(|v| v.set(true));
        for i in 0..1024 {
            let u = black_box(i as f64 / 1023.0);
            black_box(basis_functions(black_box(&knots), u).unwrap());
            black_box(basis_derivatives(black_box(&knots), u, 9).unwrap());
            black_box(basis_functions(&knots, f64::NAN)).unwrap_err();
            black_box(basis_derivatives(&knots, u, 10)).unwrap_err();
        }
        TRACK.with(|v| v.set(false));
        assert_eq!(CALLS.with(Cell::get), 0, "degree {degree}");
    }
}

#[test]
fn curve_points_derivatives_clones_and_errors_allocate_nothing() {
    for degree in 1..=9 {
        for rational in [false, true] {
            let mut values = vec![0.0; degree + 1];
            values.extend(std::iter::repeat_n(0.5, degree));
            values.extend(std::iter::repeat_n(1.0, degree + 1));
            let knots = KnotVector::new(degree, values).unwrap();
            let points: Vec<_> = (0..knots.control_point_count())
                .map(|i| [i as f64, (i % 3) as f64, 0.0])
                .collect();
            let weights = if rational {
                (0..points.len()).map(|i| (i % 5 + 1) as f64).collect()
            } else {
                vec![]
            };
            let curve = NurbsCurve3::new(knots, points, weights).unwrap();
            CALLS.with(|v| v.set(0));
            TRACK.with(|v| v.set(true));
            for i in 0..1024 {
                let u = black_box(i as f64 / 1023.0);
                let retained = black_box(curve.clone());
                black_box(retained.point(u).unwrap());
                black_box(retained.derivatives::<13>(u).unwrap());
                black_box(retained.point(f64::NAN)).unwrap_err();
                black_box(retained.derivatives::<13>(f64::NAN)).unwrap_err();
            }
            TRACK.with(|v| v.set(false));
            assert_eq!(
                CALLS.with(Cell::get),
                0,
                "degree {degree}, rational={rational}"
            );
        }
    }
}
