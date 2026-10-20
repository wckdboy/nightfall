// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use nightfall_kernel::{Kernel, KernelError};
mod plate;
fn main() -> Result<(), KernelError> {
    let kernel = Kernel::new(1e-6, 1e-9)?;
    println!("{}", kernel.info()?.identity);
    let input = plate::input();
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
