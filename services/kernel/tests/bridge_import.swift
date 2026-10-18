// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import NightfallKernelBridge

func inspectBridge(_ session: OpaquePointer?, _ document: UnsafePointer<CChar>?, _ length: Int) {
    var options = nf_mesh_options(linear_deflection: 0.1, angular_deflection: 0.2, max_triangles: 1000)
    let result = nf_session_build(session, document, length, &options)
    var count = 0
    let vertices = nf_result_vertices(result, &count)
    if let vertices, count > 0 {
        let _: Double = vertices.pointee.position.0
        let _: Double = vertices.pointee.normal.2
    }
    let faces = nf_result_faces(result, &count)
    if let faces, count > 0 {
        let _: UInt32 = faces.pointee.body_index
        let _: UInt32 = faces.pointee.first_index
        let _: UInt32 = faces.pointee.index_count
    }
    let _: UnsafePointer<UInt32>? = nf_result_indices(result, &count)
    let _: UnsafePointer<CChar>? = nf_result_json(result, &count)
    let _: Int64 = nf_session_active_feature(session)
    let _: Int64 = NF_NO_ACTIVE_FEATURE
    nf_result_free(result)
}
