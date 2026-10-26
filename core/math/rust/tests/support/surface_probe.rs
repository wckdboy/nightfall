// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
// Private differential protocol, never a production format or modeling API.
use nightfall_math::{KnotVector, NurbsSurface3};
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
        let mut f = line.split_whitespace();
        let p = next(&mut f);
        let q = next(&mut f);
        let nu: usize = next(&mut f);
        let nv: usize = next(&mut f);
        let u = next(&mut f);
        let v = next(&mut f);
        let weighted: usize = next(&mut f);
        let ku = KnotVector::new(p, (0..nu).map(|_| next(&mut f)).collect()).unwrap();
        let kv = KnotVector::new(q, (0..nv).map(|_| next(&mut f)).collect()).unwrap();
        let n = ku.control_point_count() * kv.control_point_count();
        let points = (0..n)
            .map(|_| std::array::from_fn(|_| next(&mut f)))
            .collect();
        let weights = if weighted != 0 {
            (0..n).map(|_| next(&mut f)).collect()
        } else {
            vec![]
        };
        assert!(f.next().is_none());
        let surface = NurbsSurface3::new(ku, kv, points, weights).unwrap();
        write!(out, "{}", usize::from(surface.rational())).unwrap();
        for value in surface.weights() {
            write!(out, " {value:.17e}").unwrap();
        }
        for value in surface.point(u, v).unwrap() {
            write!(out, " {value:.17e}").unwrap();
        }
        let d = surface.derivatives::<4>(u, v).unwrap();
        for (k, row) in d.iter().enumerate() {
            for point in row.iter().take(4 - k) {
                for value in point {
                    write!(out, " {value:.17e}").unwrap();
                }
            }
        }
        writeln!(out).unwrap();
    }
}
