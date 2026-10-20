// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use nightfall_kernel::*;
#[path = "plate.rs"]
mod plate;
pub struct Part {
    pub mesh: Mesh,
    pub exact: Measures,
}
fn sub(a: [f64; 3], b: [f64; 3]) -> [f64; 3] {
    std::array::from_fn(|i| a[i] - b[i])
}
fn cross(a: [f64; 3], b: [f64; 3]) -> [f64; 3] {
    [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    ]
}
fn dot(a: [f64; 3], b: [f64; 3]) -> f64 {
    (0..3).map(|i| a[i] * b[i]).sum()
}
pub fn inspect(mesh: &Mesh, exact: &Measures) -> (f64, f64) {
    assert_eq!(mesh.faces().len() as u64, exact.faces);
    assert!(!mesh.vertices().is_empty());
    assert!(!mesh.indices().is_empty());
    assert_eq!(mesh.indices().len() % 3, 0);
    let mut offset = 0;
    for (i, face) in mesh.faces().iter().enumerate() {
        assert_eq!(face.first_index as usize, offset);
        assert!(face.index_count > 0);
        assert_eq!(face.index_count % 3, 0);
        assert_eq!(face.face.kind(), EntityKind::Face);
        offset += face.index_count as usize;
        assert!(offset <= mesh.indices().len());
        assert!(
            mesh.faces()[..i]
                .iter()
                .all(|other| !other.face.same_as(&face.face).unwrap())
        );
    }
    assert_eq!(offset, mesh.indices().len());
    for v in mesh.vertices() {
        assert!(v.position.into_iter().chain(v.normal).all(f64::is_finite));
        assert!((dot(v.normal, v.normal).sqrt() - 1.).abs() <= 1e-6);
        for axis in 0..3 {
            assert!(v.position[axis] >= exact.minimum[axis] - 1e-6);
            assert!(v.position[axis] <= exact.maximum[axis] + 1e-6);
        }
    }
    let origin = mesh.vertices()[0].position;
    let (mut volume, mut area) = (0., 0.);
    for t in mesh.indices().as_chunks::<3>().0 {
        assert!(t.iter().all(|&i| (i as usize) < mesh.vertices().len()));
        let [a, b, c] = t.map(|i| mesh.vertices()[i as usize]);
        let n = cross(sub(b.position, a.position), sub(c.position, a.position));
        assert!(dot(n, n) > 0.);
        assert!(
            dot(
                n,
                std::array::from_fn(|i| a.normal[i] + b.normal[i] + c.normal[i])
            ) > 0.
        );
        volume += dot(
            sub(a.position, origin),
            cross(sub(b.position, origin), sub(c.position, origin)),
        ) / 6.;
        area += dot(n, n).sqrt() / 2.;
    }
    (volume, area)
}
pub fn build() -> Result<(String, Vec<Part>), KernelError> {
    let kernel = Kernel::new(1e-6, 1e-9)?;
    let identity = kernel.info()?.identity;
    let input = plate::input();
    let source = kernel.extrude(&input)?;
    drop(input);
    let options = MeshingOptions {
        linear_deflection: 0.02,
        angular_deflection: 0.15,
        ..MeshingOptions::default()
    };
    let mut parts = Vec::new();
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
        let exact = *placed.shape.measures();
        assert_eq!((exact.solids, exact.faces, exact.edges), (1, 14, 36));
        assert!((exact.volume - (9600. - 256. * std::f64::consts::PI)).abs() <= 1e-5);
        assert!((exact.area - (3520. + 64. * std::f64::consts::PI)).abs() <= 1e-5);
        for (a, b) in
            exact
                .centroid
                .into_iter()
                .zip([translation[0], translation[1], translation[2] + 4.])
        {
            assert!((a - b).abs() <= 1e-8);
        }
        let mesh = kernel.tessellate(&placed.shape, &options)?;
        assert_eq!(placed.history.len(), 50);
        for range in mesh.faces() {
            assert_eq!(
                placed
                    .history
                    .iter()
                    .flat_map(|row| &row.modified)
                    .filter(|entity| range.face.same_as(entity).unwrap())
                    .count(),
                1
            );
        }
        parts.push(Part { mesh, exact });
    }
    drop(source);
    drop(kernel);
    for part in &parts {
        let (v, a) = inspect(&part.mesh, &part.exact);
        assert!((v - part.exact.volume).abs() <= part.exact.volume * 0.002);
        assert!((a - part.exact.area).abs() <= part.exact.area * 0.002);
    }
    let volume: f64 = parts.iter().map(|p| p.exact.volume).sum();
    let area: f64 = parts.iter().map(|p| p.exact.area).sum();
    assert!((volume - 6. * (9600. - 256. * std::f64::consts::PI)).abs() <= 6e-5);
    assert!((area - 6. * (3520. + 64. * std::f64::consts::PI)).abs() <= 6e-5);
    Ok((identity, parts))
}
