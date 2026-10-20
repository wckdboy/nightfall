// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
// Test-only differential executable, not a production CAD or persisted format API.
use nightfall_math::{KnotVector, NurbsCurve};
use std::io::{self, Read, Write};

fn next<T: std::str::FromStr>(fields: &mut std::str::SplitWhitespace<'_>) -> T {
    fields
        .next()
        .expect("missing field")
        .parse()
        .ok()
        .expect("invalid field")
}
fn probe<const D: usize>(fields: &mut std::str::SplitWhitespace<'_>, out: &mut impl Write) {
    let degree = next(fields);
    let count: usize = next(fields);
    let u: f64 = next(fields);
    let weighted: usize = next(fields);
    let knots = KnotVector::new(degree, (0..count).map(|_| next(fields)).collect()).unwrap();
    let n = knots.control_point_count();
    let points = (0..n)
        .map(|_| std::array::from_fn(|_| next(fields)))
        .collect();
    let weights = if weighted == 1 {
        (0..n).map(|_| next(fields)).collect()
    } else {
        vec![]
    };
    assert!(fields.next().is_none());
    let curve = NurbsCurve::<D>::new(knots, points, weights).unwrap();
    write!(out, "{}", usize::from(curve.rational())).unwrap();
    for value in curve
        .weights()
        .iter()
        .copied()
        .chain(curve.point(u).unwrap())
        .chain(curve.derivatives::<13>(u).unwrap().into_iter().flatten())
    {
        write!(out, " {value:.17e}").unwrap();
    }
    writeln!(out).unwrap();
}
fn main() {
    let mut input = String::new();
    io::stdin().read_to_string(&mut input).unwrap();
    let mut out = io::BufWriter::new(io::stdout().lock());
    for line in input.lines() {
        let mut fields = line.split_whitespace();
        match next::<usize>(&mut fields) {
            2 => probe::<2>(&mut fields, &mut out),
            3 => probe::<3>(&mut fields, &mut out),
            _ => panic!("unsupported test dimension"),
        }
    }
}
