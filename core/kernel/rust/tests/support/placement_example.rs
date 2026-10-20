// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use nightfall_kernel::{Kernel, KernelError, RigidTransform};
mod plate;
fn main() -> Result<(), KernelError> {
    let kernel = Kernel::new(1e-6, 1e-9)?;
    println!("{}", kernel.info()?.identity);
    let input = plate::input();
    let source = kernel.extrude(&input)?;
    drop(input);
    let mut shapes = Vec::new();
    let mut readers = Vec::new();
    for i in 0..6 {
        let angle = f64::from(i) * std::f64::consts::PI / 3.;
        let (sine, cosine) = angle.sin_cos();
        let translation = [60. * cosine, 60. * sine, f64::from(i % 2) * 12.];
        let pose = RigidTransform {
            translation,
            axis_direction: [0., 0., 5.],
            angle,
            ..RigidTransform::default()
        };
        let result = kernel.transform(&source.shape, &pose)?;
        let m = result.shape.measures();
        for (actual, expected) in
            m.centroid
                .into_iter()
                .zip([translation[0], translation[1], translation[2] + 4.])
        {
            assert!((actual - expected).abs() <= 1e-8);
        }
        assert!((m.volume - (9600. - 256. * std::f64::consts::PI)).abs() <= 1e-5);
        assert!((m.area - (3520. + 64. * std::f64::consts::PI)).abs() <= 1e-5);
        assert_eq!((m.solids, m.faces, m.edges), (1, 14, 36));
        assert_eq!(result.history.len(), 50);
        assert!(result.generated.is_empty());
        for row in &result.history {
            assert_eq!(row.modified.len(), 1);
            assert!(row.generated.is_empty());
            assert!(!row.deleted);
            assert_eq!(row.before.kind(), row.modified[0].kind());
        }
        for face in &source.generated {
            assert_eq!(
                result
                    .history
                    .iter()
                    .filter(|h| h.before.same_as(&face.entity).unwrap())
                    .count(),
                1
            );
        }
        readers.push((
            result.history[0].before.clone(),
            result.history[0].modified[0].clone(),
        ));
        shapes.push(result.shape.clone());
    }
    drop(source);
    drop(kernel);
    let (mut volume, mut area, mut faces, mut edges, mut solids) = (0., 0., 0, 0, 0);
    for shape in &shapes {
        let m = shape.measures();
        volume += m.volume;
        area += m.area;
        solids += m.solids;
        faces += m.faces;
        edges += m.edges;
    }
    assert!((volume - 6. * (9600. - 256. * std::f64::consts::PI)).abs() <= 6e-5);
    assert!((area - 6. * (3520. + 64. * std::f64::consts::PI)).abs() <= 6e-5);
    assert_eq!((solids, faces, edges), (6, 84, 216));
    drop(shapes);
    for (before, after) in readers {
        assert!(before.same_as(&before.clone())?);
        assert!(after.same_as(&after.clone())?);
        assert!(!before.same_as(&after)?);
    }
    println!(
        "Rust kernel fixture: volume={volume:.9} mm^3 area={area:.9} mm^2 solids={solids} faces={faces} edges={edges} history=300"
    );
    Ok(())
}
