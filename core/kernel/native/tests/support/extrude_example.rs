// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use nightfall_math::{KnotVector, NurbsCurve2};
use nightfall_native::{
    Context, NativeError,
    extrusion::{Extrusion, Frame, Region},
};
fn line(a: [f64; 2], b: [f64; 2]) -> NurbsCurve2 {
    NurbsCurve2::new(
        KnotVector::new(1, vec![0.0, 0.0, 1.0, 1.0]).unwrap(),
        vec![a, b],
        vec![],
    )
    .unwrap()
}
fn hole(cx: f64) -> NurbsCurve2 {
    let w = std::f64::consts::FRAC_1_SQRT_2;
    let points = [
        [4.0, 0.0],
        [4.0, -4.0],
        [0.0, -4.0],
        [-4.0, -4.0],
        [-4.0, 0.0],
        [-4.0, 4.0],
        [0.0, 4.0],
        [4.0, 4.0],
        [4.0, 0.0],
    ];
    NurbsCurve2::new(
        KnotVector::new(
            2,
            vec![0.0, 0.0, 0.0, 1.0, 1.0, 2.0, 2.0, 3.0, 3.0, 4.0, 4.0, 4.0],
        )
        .unwrap(),
        points.map(|p| [p[0] + cx, p[1]]).to_vec(),
        vec![1.0, w, 1.0, w, 1.0, w, 1.0, w, 1.0],
    )
    .unwrap()
}
fn main() -> Result<(), NativeError> {
    let context = Context::new(1e-6, 1e-9)?;
    let input = Extrusion {
        frame: Frame::default(),
        displacement: [0.0, 0.0, 8.0],
        regions: vec![Region {
            outer: vec![
                line([-20.0, -15.0], [20.0, -15.0]),
                line([20.0, -15.0], [20.0, 15.0]),
                line([20.0, 15.0], [-20.0, 15.0]),
                line([-20.0, 15.0], [-20.0, -15.0]),
            ],
            holes: vec![vec![hole(-12.0)], vec![hole(12.0)]],
        }],
    };
    let result = context.extrude(&input)?;
    drop(input);
    drop(context);
    let shape = result.shape.clone();
    let entity = result.generated[0].entity.clone();
    drop(result);
    let m = shape.measures();
    assert!((m.volume - (9600.0 - 256.0 * std::f64::consts::PI)).abs() <= 1e-5);
    assert!((m.area - (3520.0 + 64.0 * std::f64::consts::PI)).abs() <= 1e-5);
    assert_eq!((m.solids, m.faces, m.edges), (1, 14, 36));
    assert!(entity.same_as(&entity)?);
    println!(
        "Rust headless exact plate: volume={:.9} mm^3 area={:.9} mm^2 solids={} faces={} edges={}",
        m.volume, m.area, m.solids, m.faces, m.edges
    );
    Ok(())
}
