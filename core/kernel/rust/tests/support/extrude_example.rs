// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use nightfall_kernel::{Extrusion, Frame, Kernel, KernelError, KnotVector, NurbsCurve2, Region};
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
fn main() -> Result<(), KernelError> {
    let kernel = Kernel::new(1e-6, 1e-9)?;
    println!("{}", kernel.info()?.identity);
    let input = Extrusion {
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
    };
    let mut invalid = input.clone();
    invalid.regions[0].outer.pop();
    println!(
        "Input feedback: {}",
        kernel.validate_extrusion(&invalid).unwrap_err()
    );
    let result = kernel.extrude(&input)?;
    drop(input);
    drop(kernel);
    let shape = result.shape.clone();
    let entity = result.generated[0].entity.clone();
    drop(result);
    let m = shape.measures();
    assert!((m.volume - (9600. - 256. * std::f64::consts::PI)).abs() <= 1e-5);
    assert!((m.area - (3520. + 64. * std::f64::consts::PI)).abs() <= 1e-5);
    assert_eq!((m.solids, m.faces, m.edges), (1, 14, 36));
    assert!(entity.same_as(&entity)?);
    println!(
        "Rust kernel exact plate: volume={:.9} mm^3 area={:.9} mm^2 solids={} faces={} edges={}",
        m.volume, m.area, m.solids, m.faces, m.edges
    );
    Ok(())
}
