// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#define NF_NATIVE_NOEXCEPT noexcept
#else
#define NF_NATIVE_NOEXCEPT
#endif
#define NF_NATIVE_ABI_VERSION UINT32_C(1)
enum {
  NF_NATIVE_OK,
  NF_NATIVE_INVALID,
  NF_NATIVE_UNAVAILABLE,
  NF_NATIVE_FAILURE,
  NF_NATIVE_ABI_MISMATCH
};
typedef struct nf_native_context nf_native_context;
typedef struct nf_native_result nf_native_result;
typedef struct nf_native_entity nf_native_entity;
typedef struct {
  double x, y;
} nf_native_point2;
typedef struct {
  uint32_t degree;
  const double* knots;
  size_t knot_count;
  const nf_native_point2* points;
  size_t point_count;
  const double* weights;
  size_t weight_count;
} nf_native_curve2;
typedef struct {
  const nf_native_curve2* curves;
  size_t count;
} nf_native_loop2;
typedef struct {
  nf_native_loop2 outer;
  const nf_native_loop2* holes;
  size_t hole_count;
} nf_native_region2;
typedef struct {
  double origin[3], x_axis[3], normal[3], displacement[3];
  const nf_native_region2* regions;
  size_t region_count;
} nf_native_extrusion;
typedef struct {
  double volume, area, centroid[3], minimum[3], maximum[3];
  uint64_t solids, faces, edges, estimated_bytes, generated_count, history_count;
} nf_native_result_info;
typedef struct {
  const uint8_t* role;
  size_t role_bytes;
  const nf_native_entity* entity;
  uint64_t region, loop, curve, section;
  uint32_t has_from, kind;  // kind: 1 face, 2 edge
} nf_native_generated;
typedef struct {
  double translation[3], axis_origin[3], axis_direction[3], angle;
} nf_native_rigid_transform;
typedef struct {
  const nf_native_entity* entity;
  uint32_t kind;  // 1 face, 2 edge
} nf_native_entity_view;
typedef struct {
  const nf_native_entity* before;
  uint64_t modified_count, generated_count;
  uint32_t before_kind, deleted;  // deleted is 0 or 1
} nf_native_history;
typedef struct {
  uint32_t abi_version, capabilities;
  const uint8_t* identity;
  size_t identity_bytes;
} nf_native_info;
// Service-private ABI (RFC 0008). All calls are serialized, including create/release.
// Raw callers must supply aligned, live pointers from this adapter; forged/stale pointers
// cannot be checked. Keep each owner on its creating OS thread. No function throws.
// Linear/angular tolerances are mm/radians; OCCT precision floors remain binding.
// Failure clears *out; success transfers one owner, released only by context_release.
uint32_t nf_native_context_create(uint32_t abi, double linear, double angular,
                                  nf_native_context** out) NF_NATIVE_NOEXCEPT;
// Clears *out on failure. Identity is immutable UTF-8, NOT NUL-terminated by contract,
// borrowed until release. Capability bits 0..8: extrude, rigid transform, booleans,
// revolve, mirror, chamfer, fillet, loft, sweep. These describe the native solver.
uint32_t nf_native_context_info(const nf_native_context*, nf_native_info* out) NF_NATIVE_NOEXCEPT;
void nf_native_context_release(nf_native_context*) NF_NATIVE_NOEXCEPT;  // NULL-safe
// Exact, immutable extrusion of explicit CCW outer / CW hole loops, degree 1..9.
// All input ranges are aligned, live, immutable and borrowed only during this call.
// Empty weights mean all 1; knots must fit the backend's signed-index arithmetic.
// Geometry validation/unsupported errors return FAILURE; malformed records return INVALID.
// Failure clears *out. Success owns the COMPLETE result and backend after context release.
uint32_t nf_native_extrude(nf_native_context*, const nf_native_extrusion*,
                           nf_native_result** out) NF_NATIVE_NOEXCEPT;
// Rotate about a finite nonzero axis/origin, then translate (mm/radians). Direction need
// not be unit. Source is borrowed for this call; result retains every history snapshot.
// Foreign contexts return INVALID before geometry access. Native validation stays binding.
// Input stays immutable; failure clears *out. Success uses the existing result release.
uint32_t nf_native_transform(nf_native_context*, const nf_native_result* source,
                             const nf_native_rigid_transform*,
                             nf_native_result** out) NF_NATIVE_NOEXCEPT;
// Queries clear outputs on failure. Exact measures use mm units; estimates are not RSS limits.
uint32_t nf_native_result_query(const nf_native_result*,
                                nf_native_result_info* out) NF_NATIVE_NOEXCEPT;
// Index is transient, never a persisted selector. Role and entity are borrowed until result
// release. Source indices address this call's regions/loops/curves; section is 0 for extrusion.
uint32_t nf_native_result_generated(const nf_native_result*, uint64_t index,
                                    nf_native_generated* out) NF_NATIVE_NOEXCEPT;
// Complete native history; borrowed entities stay valid until this RESULT is released,
// even after the original source/context is released. Indices are never persisted selectors.
uint32_t nf_native_result_history(const nf_native_result*, uint64_t index,
                                  nf_native_history* out) NF_NATIVE_NOEXCEPT;
// list: 0 modified, 1 generated. Bounds/invalid lists clear *out and return INVALID.
uint32_t nf_native_result_history_entity(const nf_native_result*, uint64_t history, uint32_t list,
                                         uint64_t index,
                                         nf_native_entity_view* out) NF_NATIVE_NOEXCEPT;
uint32_t nf_native_entities_same(const nf_native_entity*, const nf_native_entity*,
                                 uint32_t* out) NF_NATIVE_NOEXCEPT;
void nf_native_result_release(nf_native_result*) NF_NATIVE_NOEXCEPT;  // NULL-safe
#ifdef __cplusplus
}
#endif
#undef NF_NATIVE_NOEXCEPT
