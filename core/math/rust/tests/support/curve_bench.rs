// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
// Equivalent inputs to tests/rust_curve_bench.cpp; run only on an uncontended host.
use nightfall_math::{KnotVector, NurbsCurve3};
use std::hint::black_box;
use std::time::Instant;

fn main() {
    let mut knots = vec![0.0; 4];
    knots.extend((1..=16).map(|i| i as f64 / 17.0));
    knots.extend([1.0; 4]);
    let knots = KnotVector::new(3, knots).unwrap();
    let points: Vec<_> = (0..20)
        .map(|i| {
            [
                i as f64 * 0.25,
                (i % 7) as f64 - 3.0,
                (i * i % 11) as f64 * 0.125,
            ]
        })
        .collect();
    let us: Vec<_> = (0_u64..1_000_000)
        .map(|i| ((i * 16807 + 7) % 1_000_003) as f64 / 1_000_003.0)
        .collect();
    for rational in [false, true] {
        let weights = if rational {
            (0..20).map(|i| (i % 7 + 1) as f64 * 0.25).collect()
        } else {
            vec![]
        };
        let curve = NurbsCurve3::new(knots.clone(), points.clone(), weights).unwrap();
        let mut runs = [0.0; 5];
        let mut checksum = 0.0;
        for run in &mut runs {
            let start = Instant::now();
            let mut sum = 0.0;
            for u in &us {
                let p = black_box(black_box(&curve).point(*u).unwrap());
                sum += p[0] + p[1] + p[2];
            }
            *run = start.elapsed().as_nanos() as f64 / us.len() as f64;
            checksum += black_box(sum);
        }
        runs.sort_by(f64::total_cmp);
        println!(
            "Rust rational={rational}: median {:.2} ns (range {:.2}–{:.2}), checksum {checksum:.17e}",
            runs[2], runs[0], runs[4]
        );
    }
}
