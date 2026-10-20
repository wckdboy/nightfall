// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//! Retained immutable native mesh buffers and original-face picking (RFC 0008).
use crate::{
    Context, NativeError, NonNull, Rc, c_void,
    extrusion::{Entity, Shape},
    mesh_ffi as ffi, status, with_native,
};
/// Absolute mm/radian deflections; native precision floors and triangle/vertex budgets apply.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct MeshingOptions {
    pub linear_deflection: f64,
    pub angular_deflection: f64,
    pub max_triangles: u64,
}
impl Default for MeshingOptions {
    fn default() -> Self {
        Self {
            linear_deflection: 0.1,
            angular_deflection: 0.2,
            max_triangles: 1_000_000,
        }
    }
}
/// World-space position in mm and outward unit normal; shared within each original face.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Vertex {
    pub position: [f64; 3],
    pub normal: [f64; 3],
}
/// Nonempty triangle range identifying an ORIGINAL face. Offsets are transient, not selectors.
/// Cloning the face independently retains its native mesh, source snapshot and backend.
#[derive(Clone)]
pub struct MeshFace {
    pub face: Entity,
    pub first_index: u32,
    pub index_count: u32,
}
/// Immutable, thread-affine native mesh. Clone retains the same native vertex/index buffers.
///
/// ```compile_fail
/// use nightfall_native::mesh::Mesh;
/// fn move_mesh(mesh: Mesh) { std::thread::spawn(move || mesh.vertices().len()); }
/// ```
#[derive(Clone)]
pub struct Mesh {
    owner: Rc<Owner>,
    faces: Vec<MeshFace>,
}
pub(crate) struct Owner {
    pointer: NonNull<c_void>,
    _source: Shape,
    info: ffi::Info,
}
impl Drop for Owner {
    fn drop(&mut self) {
        // SAFETY: final retained allocation, paired release. Source/context drop AFTER the guard.
        with_native(|| unsafe { ffi::nf_native_mesh_release(self.pointer.as_ptr()) });
    }
}
impl Mesh {
    /// Borrow native immutable storage without copying; this mesh retains its allocation.
    pub fn vertices(&self) -> &[Vertex] {
        // SAFETY: checked nonempty aligned bounded C-layout buffer, immutable until owner release.
        unsafe {
            std::slice::from_raw_parts(
                self.owner.info.vertices,
                self.owner.info.vertex_count as usize,
            )
        }
    }
    /// Outward triangle triples, indexing vertices; borrowed for this retained mesh's lifetime.
    pub fn indices(&self) -> &[u32] {
        // SAFETY: checked aligned bounded native buffer retained by self, never mutated.
        unsafe {
            std::slice::from_raw_parts(
                self.owner.info.indices,
                self.owner.info.index_count as usize,
            )
        }
    }
    /// Contiguous nonempty original-face ranges covering every triangle exactly once.
    pub fn faces(&self) -> &[MeshFace] {
        &self.faces
    }
}
fn check_buffer<T>(pointer: *const T, count: u64) -> Result<(), NativeError> {
    if pointer.is_null()
        || !pointer.is_aligned()
        || count == 0
        || count > 3_000_000
        || count > (isize::MAX as usize / std::mem::size_of::<T>()) as u64
    {
        return Err(NativeError::AbiMismatch);
    }
    Ok(())
}
impl Context {
    /// Mesh an immutable copy using the original native mesher. Foreign contexts fail first;
    /// native validation/failures/budgets remain binding. No partial mesh is published.
    pub fn tessellate(&self, shape: &Shape, options: &MeshingOptions) -> Result<Mesh, NativeError> {
        let source = shape.native_pointer(self)?;
        let pointer = with_native(|| {
            let mut out = std::ptr::null_mut();
            // SAFETY: retained same-context source, aligned borrowed options and writable output.
            status(unsafe {
                ffi::nf_native_tessellate(self.0.0.as_ptr(), source.as_ptr(), options, &mut out)
            })?;
            NonNull::new(out).ok_or(NativeError::NativeFailure)
        })?;
        // Construct outside the guard so every decoding error releases without recursive locking.
        let mut owner = Owner {
            pointer,
            _source: shape.clone(),
            info: ffi::Info::default(),
        };
        with_native(|| {
            // SAFETY: live retained mesh and writable aligned output; buffers remain immutable.
            status(unsafe { ffi::nf_native_mesh_query(pointer.as_ptr(), &mut owner.info) })
        })?;
        let info = &owner.info;
        check_buffer(info.vertices, info.vertex_count)?;
        check_buffer(info.indices, info.index_count)?;
        if !info.index_count.is_multiple_of(3)
            || info.face_count == 0
            || info.face_count > info.index_count / 3
            || info.face_count != shape.measures().faces
        {
            return Err(NativeError::AbiMismatch);
        }
        let owner = Rc::new(owner);
        let mut mesh = Mesh {
            owner: owner.clone(),
            faces: Vec::new(),
        };
        if mesh
            .indices()
            .iter()
            .any(|&i| u64::from(i) >= owner.info.vertex_count)
            || mesh
                .vertices()
                .iter()
                .any(|v| !v.position.into_iter().chain(v.normal).all(f64::is_finite))
        {
            return Err(NativeError::AbiMismatch);
        }
        let mut offset = 0u64;
        for index in 0..owner.info.face_count {
            let mut face = ffi::Face::default();
            with_native(|| {
                // SAFETY: native mesh retains original face objects; checked index/aligned output.
                status(unsafe { ffi::nf_native_mesh_face_at(pointer.as_ptr(), index, &mut face) })
            })?;
            if u64::from(face.first_index) != offset
                || face.index_count == 0
                || !face.index_count.is_multiple_of(3)
            {
                return Err(NativeError::AbiMismatch);
            }
            offset += u64::from(face.index_count);
            if offset > owner.info.index_count {
                return Err(NativeError::AbiMismatch);
            }
            mesh.faces.push(MeshFace {
                face: shape.mesh_entity(face.face, owner.clone())?,
                first_index: face.first_index,
                index_count: face.index_count,
            });
        }
        if offset != owner.info.index_count {
            return Err(NativeError::AbiMismatch);
        }
        Ok(mesh)
    }
}
#[cfg(test)]
#[path = "../tests/support/mesh_abi.rs"]
mod tests;
