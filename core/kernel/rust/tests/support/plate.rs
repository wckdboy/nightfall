// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use nightfall_kernel::{Extrusion, Frame, KnotVector, NurbsCurve2, Region};
fn line(a: [f64; 2], b: [f64; 2]) -> NurbsCurve2 {
    NurbsCurve2::new(
        KnotVector::new(1, vec![0., 0., 1., 1.]).unwrap(),
        vec![a, b],
        vec![],
    )
    .unwrap()
}
fn hole(cx: f64) -> NurbsCurve2 {
    let w = std::f64::consts::FRAC_1_SQRT_2;
    let points = [
        [4., 0.],
        [4., -4.],
        [0., -4.],
        [-4., -4.],
        [-4., 0.],
        [-4., 4.],
        [0., 4.],
        [4., 4.],
        [4., 0.],
    ];
    NurbsCurve2::new(
        KnotVector::new(2, vec![0., 0., 0., 1., 1., 2., 2., 3., 3., 4., 4., 4.]).unwrap(),
        points.map(|p| [p[0] + cx, p[1]]).to_vec(),
        vec![1., w, 1., w, 1., w, 1., w, 1.],
    )
    .unwrap()
}
pub fn input() -> Extrusion {
    Extrusion {
        frame: Frame::default(),
        displacement: [0., 0., 8.],
        regions: vec![Region {
            outer: vec![
                line([-20., -15.], [20., -15.]),
                line([20., -15.], [20., 15.]),
                line([20., 15.], [-20., 15.]),
                line([-20., 15.], [-20., -15.]),
            ],
            holes: vec![vec![hole(-12.)], vec![hole(12.)]],
        }],
    }
}
