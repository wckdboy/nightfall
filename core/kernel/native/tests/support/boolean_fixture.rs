// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use nightfall_math::{KnotVector, NurbsCurve2};
use nightfall_native::extrusion::{Extrusion, Frame, Region};
pub fn box_input(origin: [f64; 3], size: [f64; 3]) -> Extrusion {
    let points = [
        [0., 0.],
        [size[0], 0.],
        [size[0], size[1]],
        [0., size[1]],
        [0., 0.],
    ];
    Extrusion {
        frame: Frame {
            origin,
            ..Frame::default()
        },
        displacement: [0., 0., size[2]],
        regions: vec![Region {
            outer: points
                .windows(2)
                .map(|p| {
                    NurbsCurve2::new(
                        KnotVector::new(1, vec![0., 0., 1., 1.]).unwrap(),
                        p.to_vec(),
                        vec![],
                    )
                    .unwrap()
                })
                .collect(),
            holes: vec![],
        }],
    }
}
pub fn cylinder_input() -> Extrusion {
    let points = [
        [1., 0.],
        [1., 1.],
        [0., 1.],
        [-1., 1.],
        [-1., 0.],
        [-1., -1.],
        [0., -1.],
        [1., -1.],
        [1., 0.],
    ];
    let w = std::f64::consts::FRAC_1_SQRT_2;
    let curve = NurbsCurve2::new(
        KnotVector::new(2, vec![0., 0., 0., 1., 1., 2., 2., 3., 3., 4., 4., 4.]).unwrap(),
        points.map(|p| [5. + 2. * p[0], 5. + 2. * p[1]]).to_vec(),
        vec![1., w, 1., w, 1., w, 1., w, 1.],
    )
    .unwrap();
    Extrusion {
        frame: Frame {
            origin: [0., 0., -1.],
            ..Frame::default()
        },
        displacement: [0., 0., 12.],
        regions: vec![Region {
            outer: vec![curve],
            holes: vec![],
        }],
    }
}
