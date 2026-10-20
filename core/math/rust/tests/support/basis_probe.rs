// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
// Differential-test executable only; this is not a CAD/client API or persisted format.
use nightfall_math::{KnotVector, basis_derivatives, basis_functions};
use std::io::{self, Read, Write};

fn next<T: std::str::FromStr>(fields: &mut std::str::SplitWhitespace<'_>) -> T {
    fields
        .next()
        .expect("missing field")
        .parse()
        .ok()
        .expect("invalid field")
}
fn main() {
    let mut input = String::new();
    io::stdin().read_to_string(&mut input).unwrap();
    let mut out = io::BufWriter::new(io::stdout().lock());
    for line in input.lines() {
        let mut fields = line.split_whitespace();
        let degree: usize = next(&mut fields);
        let count: usize = next(&mut fields);
        let u: f64 = next(&mut fields);
        let order: usize = next(&mut fields);
        let knots =
            KnotVector::new(degree, (0..count).map(|_| next(&mut fields)).collect()).unwrap();
        assert!(fields.next().is_none());
        let basis = basis_functions(&knots, u).unwrap();
        let derivatives = basis_derivatives(&knots, u, order).unwrap();
        write!(out, "{}", basis.span).unwrap();
        for value in basis
            .values
            .iter()
            .chain(derivatives.values.iter().flatten())
        {
            write!(out, " {value:.17e}").unwrap();
        }
        writeln!(out).unwrap();
    }
}
