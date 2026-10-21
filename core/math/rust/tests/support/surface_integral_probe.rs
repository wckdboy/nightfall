// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
// Private quadrature protocol for native control nets; no native math calls.
use nightfall_math::{KnotVector, NurbsSurface3};
use std::io::{self, Read, Write};

fn next<T: std::str::FromStr>(f: &mut std::str::SplitWhitespace<'_>) -> T {
    f.next()
        .expect("missing field")
        .parse()
        .ok()
        .expect("invalid field")
}
#[derive(Default)]
struct Sum {
    value: f64,
    correction: f64,
}
impl Sum {
    fn add(&mut self, x: f64) {
        let y = x - self.correction;
        let t = self.value + y;
        self.correction = (t - self.value) - y;
        self.value = t;
    }
}
fn legendre(x: f64) -> (f64, f64) {
    let (mut a, mut b) = (1., x);
    for n in 2..=8 {
        (a, b) = (
            b,
            ((2 * n - 1) as f64 * x * b - (n - 1) as f64 * a) / n as f64,
        );
    }
    (b, 8. * (x * b - a) / (x * x - 1.))
}
fn gauss() -> [(f64, f64); 8] {
    let mut rule = [(0., 0.); 8];
    for i in 0..4 {
        let mut x = (std::f64::consts::PI * (i as f64 + 0.75) / 8.5).cos();
        for _ in 0..16 {
            let (p, d) = legendre(x);
            let step = p / d;
            x -= step;
            if step.abs() <= 4. * f64::EPSILON {
                break;
            }
        }
        let (p, d) = legendre(x);
        assert!(p.abs() < 4e-15);
        let w = 2. / ((1. - x * x) * d * d);
        rule[2 * i] = (-x, w);
        rule[2 * i + 1] = (x, w);
    }
    for n in 0..16 {
        let value: f64 = rule.iter().map(|(x, w)| w * x.powi(n)).sum();
        let exact = if n % 2 == 0 { 2. / (n + 1) as f64 } else { 0. };
        assert!(
            (value - exact).abs() < 2e-14,
            "quadrature polynomial degree {n}"
        );
    }
    rule
}
fn spans(k: &KnotVector) -> Vec<(f64, f64)> {
    k.knots()
        .windows(2)
        .filter(|x| x[0] < x[1])
        .map(|x| (x[0], x[1]))
        .collect()
}
fn integrate(s: &NurbsSurface3, sign: f64, n: usize, rule: &[(f64, f64); 8]) -> (f64, f64) {
    let (mut volume, mut area) = (Sum::default(), Sum::default());
    for (a, b) in spans(s.knots_u()) {
        for (c, d) in spans(s.knots_v()) {
            for i in 0..n {
                for j in 0..n {
                    let hu = (b - a) / (2 * n) as f64;
                    let hv = (d - c) / (2 * n) as f64;
                    let cu = a + (2 * i + 1) as f64 * hu;
                    let cv = c + (2 * j + 1) as f64 * hv;
                    for (x, wx) in rule {
                        for (y, wy) in rule {
                            let v = s.derivatives::<2>(cu + x * hu, cv + y * hv).unwrap();
                            let (p, du, dv) = (v[0][0], v[1][0], v[0][1]);
                            let cross = [
                                du[1] * dv[2] - du[2] * dv[1],
                                du[2] * dv[0] - du[0] * dv[2],
                                du[0] * dv[1] - du[1] * dv[0],
                            ];
                            let weight = wx * wy * hu * hv;
                            area.add(cross.iter().map(|x| x * x).sum::<f64>().sqrt() * weight);
                            // div((x-500)/2,y/2,0)=1; horizontal caps have zero flux.
                            volume.add(
                                ((p[0] - 500.) * cross[0] + p[1] * cross[1]) * 0.5 * sign * weight,
                            );
                        }
                    }
                }
            }
        }
    }
    assert!(volume.value.is_finite() && area.value.is_finite() && area.value > 0.);
    (volume.value, area.value)
}
fn main() {
    let rule = gauss();
    let mut text = String::new();
    io::stdin().read_to_string(&mut text).unwrap();
    let mut out = io::BufWriter::new(io::stdout().lock());
    for line in text.lines() {
        let mut f = line.split_whitespace();
        let p = next(&mut f);
        let q = next(&mut f);
        let nu: usize = next(&mut f);
        let nv: usize = next(&mut f);
        let sign = next(&mut f);
        let samples: usize = next(&mut f);
        let ku = KnotVector::new(p, (0..nu).map(|_| next(&mut f)).collect()).unwrap();
        let kv = KnotVector::new(q, (0..nv).map(|_| next(&mut f)).collect()).unwrap();
        let count = ku.control_point_count() * kv.control_point_count();
        let points = (0..count)
            .map(|_| std::array::from_fn(|_| next(&mut f)))
            .collect();
        let weights = (0..count).map(|_| next(&mut f)).collect();
        let s = NurbsSurface3::new(ku, kv, points, weights).unwrap();
        for n in [1, 2, 4, 8, 16] {
            let (volume, area) = integrate(&s, sign, n, &rule);
            write!(out, "{volume:.17e} {area:.17e} ").unwrap();
        }
        for _ in 0..samples {
            let u = next(&mut f);
            let v = next(&mut f);
            let d = s.derivatives::<2>(u, v).unwrap();
            for value in d[0][0].into_iter().chain(d[1][0]).chain(d[0][1]) {
                write!(out, "{value:.17e} ").unwrap();
            }
        }
        assert!(f.next().is_none());
        writeln!(out).unwrap();
    }
}
