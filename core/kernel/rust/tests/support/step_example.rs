// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//! Exact derived geometry example; editable Nightfall documents remain the source of truth.
use std::io::Write;
mod step_fixture;
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let arguments: Vec<_> = std::env::args_os().skip(1).collect();
    let output = match arguments.as_slice() {
        [] => None,
        [flag, file] if flag == "--output" => Some(std::path::PathBuf::from(file)),
        _ => return Err("usage: nf-kernel-step [--output NEW_FILE.step]".into()),
    };
    let (identity, step) = step_fixture::build()?;
    // All geometry/facade owners have ended. Only independent retained bytes remain.
    println!("{identity}");
    println!(
        "Rust STEP fixture: solids=6 faces=84 edges=216 bytes={}",
        step.bytes().len()
    );
    println!(
        "source volume={:.9} mm^3 area={:.9} mm^2",
        6. * (9600. - 256. * std::f64::consts::PI),
        6. * (3520. + 64. * std::f64::consts::PI)
    );
    if let Some(output) = output {
        let mut file = std::fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&output)?;
        file.write_all(step.bytes())?;
        println!("Exact AP242 millimetre geometry: {}", output.display());
    }
    Ok(())
}
