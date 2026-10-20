// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//! Derived validation output, not a Nightfall source document or production client.
use std::fmt::Write;
mod mesh_fixture;
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let arguments: Vec<_> = std::env::args_os().skip(1).collect();
    let output = match arguments.as_slice() {
        [] => None,
        [flag, directory] if flag == "--output-dir" => Some(std::path::PathBuf::from(directory)),
        _ => return Err("usage: nf-kernel-mesh [--output-dir NEW_DIRECTORY]".into()),
    };
    let (identity, parts) = mesh_fixture::build()?;
    let (mut exact_volume, mut exact_area, mut volume, mut area) = (0., 0., 0., 0.);
    let (mut vertices, mut triangles, mut faces) = (0, 0, 0);
    for part in &parts {
        let (v, a) = mesh_fixture::inspect(&part.mesh, &part.exact);
        exact_volume += part.exact.volume;
        exact_area += part.exact.area;
        volume += v;
        area += a;
        vertices += part.mesh.vertices().len();
        triangles += part.mesh.indices().len() / 3;
        faces += part.mesh.faces().len();
    }
    let receipt = format!(
        "{identity}\nRust mesh fixture: solids={} faces={faces} vertices={vertices} triangles={triangles}\nexact volume={exact_volume:.9} mm^3 area={exact_area:.9} mm^2\nmesh volume={volume:.9} mm^3 area={area:.9} mm^2\n",
        parts.len()
    );
    print!("{receipt}");
    let Some(output) = output else { return Ok(()) };
    let mut data = format!(
        "{{\"exactVolume\":{exact_volume},\"exactArea\":{exact_area},\"meshVolume\":{volume},\"meshArea\":{area},\"vertices\":{vertices},\"triangles\":{triangles},\"parts\":["
    );
    let mut obj = String::from(
        "# Nightfall Rust mesh validation fixture (mm)\n# Derived geometry; face groups are transient, never persisted selectors.\n",
    );
    let mut offset = 1usize;
    for (i, part) in parts.iter().enumerate() {
        if i > 0 {
            data.push(',');
        }
        data.push_str("{\"vertices\":[");
        writeln!(obj, "o plate_{:02}", i + 1)?;
        for (j, v) in part.mesh.vertices().iter().enumerate() {
            if j > 0 {
                data.push(',');
            }
            let values: Vec<_> = v.position.into_iter().chain(v.normal).collect();
            write!(data, "{values:?}")?;
            writeln!(
                obj,
                "v {} {} {}\nvn {} {} {}",
                v.position[0], v.position[1], v.position[2], v.normal[0], v.normal[1], v.normal[2]
            )?;
        }
        write!(data, "],\"indices\":{:?},\"faces\":[", part.mesh.indices())?;
        for (f, range) in part.mesh.faces().iter().enumerate() {
            if f > 0 {
                data.push(',');
            }
            write!(data, "[{},{}]", range.first_index, range.index_count)?;
            writeln!(obj, "g plate_{:02}_face_{f}", i + 1)?;
            let start = range.first_index as usize;
            let end = start + range.index_count as usize;
            for t in part.mesh.indices()[start..end].as_chunks::<3>().0 {
                let [a, b, c] = t.map(|index| index as usize + offset);
                writeln!(obj, "f {a}//{a} {b}//{b} {c}//{c}")?;
            }
        }
        data.push_str("]}");
        offset += part.mesh.vertices().len();
    }
    data.push_str("]}");
    let html = include_str!("mesh_preview.html").replace("__DATA__", &data);
    // An existing destination is an error: never overwrite an archived visual artifact.
    std::fs::create_dir(&output)?;
    std::fs::write(output.join("fixture.obj"), obj)?;
    std::fs::write(output.join("preview.html"), html)?;
    std::fs::write(output.join("receipt.txt"), receipt)?;
    println!("Derived OBJ/preview/receipt: {}", output.display());
    Ok(())
}
