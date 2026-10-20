// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use nightfall_kernel::*;
#[path = "plate.rs"]
pub mod plate;
pub fn build() -> Result<(String, Step), KernelError> {
    let kernel = Kernel::new(1e-6, 1e-9)?;
    let identity = kernel.info()?.identity;
    let input = plate::input();
    let source = kernel.extrude(&input)?;
    let mut shapes = Vec::new();
    for i in 0..6 {
        let angle = f64::from(i) * std::f64::consts::PI / 3.;
        let (sine, cosine) = angle.sin_cos();
        let translation = [60. * cosine, 60. * sine, f64::from(i % 2) * 12.];
        let placed = kernel.transform(
            &source.shape,
            &RigidTransform {
                translation,
                axis_direction: [0., 0., 5.],
                angle,
                ..RigidTransform::default()
            },
        )?;
        let m = placed.shape.measures();
        assert_eq!((m.solids, m.faces, m.edges), (1, 14, 36));
        assert!((m.volume - (9600. - 256. * std::f64::consts::PI)).abs() <= 1e-5);
        assert!((m.area - (3520. + 64. * std::f64::consts::PI)).abs() <= 1e-5);
        for (actual, expected) in
            m.centroid
                .into_iter()
                .zip([translation[0], translation[1], translation[2] + 4.])
        {
            assert!((actual - expected).abs() <= 1e-8);
        }
        shapes.push(placed.shape);
    }
    let step = kernel.export_step(&shapes)?;
    drop(shapes);
    drop(source);
    drop(input);
    drop(kernel);
    Ok((identity, step))
}
