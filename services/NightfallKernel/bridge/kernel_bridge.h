// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// SPIKE (#8): C API between the Swift XPC service and the C++ kernel (OCCT for now).
// Plain C so neither Swift nor callers ever see kernel headers.

#ifndef NF_KERNEL_BRIDGE_H
#define NF_KERNEL_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nf_part nf_part;  // opaque: one body under construction

/// Flat-shaded triangle mesh: 3 floats position + 3 floats normal per vertex,
/// 3 vertices per triangle (no index sharing).
typedef struct {
  float* vertices;  // vertex_count * 6 floats
  size_t vertex_count;
  double volume;  // mm³ of the exact B-rep
} nf_mesh;

nf_part* nf_part_box(double dx, double dy, double dz);
/// Fillet every edge. Returns 0 on success.
int nf_part_fillet_all(nf_part* part, double radius);
/// Through-hole along Z at (x, y). Returns 0 on success.
int nf_part_hole(nf_part* part, double x, double y, double radius);
/// Tessellate (chordal deviation in mm). Returns 0 on success; free with nf_mesh_free.
int nf_part_mesh(const nf_part* part, double deflection, nf_mesh* out);
void nf_part_free(nf_part* part);
void nf_mesh_free(nf_mesh* mesh);

#ifdef __cplusplus
}
#endif

#endif
