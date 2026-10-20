// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use crate::{
    c_void,
    mesh::{MeshingOptions, Vertex},
};
#[repr(C)]
#[derive(Default)]
pub(crate) struct Info {
    pub vertices: *const Vertex,
    pub indices: *const u32,
    pub vertex_count: u64,
    pub index_count: u64,
    pub face_count: u64,
}
#[repr(C)]
#[derive(Default)]
pub(crate) struct Face {
    pub face: *const c_void,
    pub first_index: u32,
    pub index_count: u32,
}
unsafe extern "C" {
    pub(crate) fn nf_native_tessellate(
        context: *mut c_void,
        source: *const c_void,
        options: *const MeshingOptions,
        out: *mut *mut c_void,
    ) -> u32;
    pub(crate) fn nf_native_mesh_query(mesh: *const c_void, out: *mut Info) -> u32;
    pub(crate) fn nf_native_mesh_face_at(mesh: *const c_void, index: u64, out: *mut Face) -> u32;
    pub(crate) fn nf_native_mesh_release(mesh: *mut c_void);
}
