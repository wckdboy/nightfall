// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef struct nf_session nf_session;
typedef struct nf_result nf_result;
typedef struct {
  double position[3], normal[3];  // world-space mm, outward unit surface normal
} nf_vertex;
typedef struct {
  uint32_t body_index, first_index, index_count;  // contiguous outward triangle triples
} nf_face;
typedef struct {
  double linear_deflection, angular_deflection;  // absolute mm, radians; kernel tolerance floors
  uint32_t max_triangles;                        // 1..1,000,000 across ALL live bodies
} nf_mesh_options;
#define NF_NO_ACTIVE_FEATURE (-1LL)

// Service-private ABI (RFC 0003). No functions throw. Serialize all builds on the service
// worker; sessions own per-document caches (96 entries / 64 MiB estimated, not an RSS cap).
// Creation returns NULL on failure. Destroy only after workers/watchdog readers have stopped.
nf_session* nf_session_create(void);
void nf_session_destroy(nf_session*);
// Synchronously evaluate v1 UTF-8 JSON (<=16 MiB), then mesh every live body in ID order.
// Source is borrowed only during this call; byte count excludes any terminating NUL.
// NULL options select 0.1 mm / 0.2 rad / 1,000,000 triangles. Bounds cap returned data,
// not OCCT working memory/time; XPC admission/watchdog remain the service's responsibility.
// Owned results survive later builds/session teardown. NULL means allocation/diagnostic
// failure. Other failures have errors and ZERO geometry, including partial feature success.
// A valid empty/sketch-only/suppressed/rolled-back document succeeds with zero bodies and
// empty buffers. Consumers distinguish it from failure by checking the manifest errors.
nf_result* nf_session_build(nf_session*, const char*, size_t, const nf_mesh_options*);
// May be polled concurrently with build. Zero-based authored feature index, or -1
// during validation, meshing and idle. Only this accessor supports concurrent session access.
int64_t nf_session_active_feature(const nf_session*);
// Immutable borrowed views, valid until nf_result_free. Optional counts receive zero on NULL
// results; empty buffers return NULL. JSON is NUL-terminated, <=16 MiB, with count EXCLUDING NUL.
// Manifest: errors[{code,message,pointer,offset?,feature_index?}], features[{id,status,hash?}],
// bodies[{id,hash,volume,area,centroid,minimum,maximum,solids,faces,edges}], face_names and cache
// {hits,kernel_calls,binding_misses,entries,estimated_bytes}. Quantities use mm units.
// face_names[i] lists {feature,role,from?} for faces[i]; names need not uniquely select a face.
// Body/face/vertex indices are request-local associations: never persist them in IR/selectors.
// This manifest is ephemeral service transport, not a .nightfall file or script API.
const char* nf_result_json(const nf_result*, size_t* bytes);
const nf_vertex* nf_result_vertices(const nf_result*, size_t* count);
const uint32_t* nf_result_indices(const nf_result*, size_t* count);
const nf_face* nf_result_faces(const nf_result*, size_t* count);
void nf_result_free(nf_result*);  // NULL-safe, as is nf_session_destroy
#ifdef __cplusplus
}
#endif
