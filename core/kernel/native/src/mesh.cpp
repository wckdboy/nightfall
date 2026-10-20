// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include <limits>
#include "guard.hpp"
#include "result.hpp"
#if NF_NATIVE_OCCT
struct nf_native_mesh {
  std::shared_ptr<nf::kernel::IKernel> backend;
  nf::kernel::ShapePtr source;
  std::vector<nf_native_mesh_vertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<nf::kernel::MeshFace> faces;
};
#else
struct nf_native_mesh {};
#endif
uint32_t nf_native_tessellate(nf_native_context* context, const nf_native_result* source,
                              const nf_native_meshing_options* options,
                              nf_native_mesh** out) noexcept {
  nf::native::clear_error();
  if (out) *out = nullptr;
  if (!context || !source || !options || !out ||
      reinterpret_cast<uintptr_t>(options) % alignof(nf_native_meshing_options))
    return NF_NATIVE_INVALID;
  return nf::native::guarded([&]() -> uint32_t {
#if NF_NATIVE_OCCT
    if (source->backend != context->kernel) return NF_NATIVE_INVALID;
    if (options->max_triangles > std::numeric_limits<size_t>::max()) return NF_NATIVE_INVALID;
    auto mesh = context->kernel->tessellate(
        source->operation.shape, {options->linear_deflection, options->angular_deflection,
                                  static_cast<size_t>(options->max_triangles)});
    if (!mesh) return nf::native::failure(mesh.error().code, mesh.error().message);
    auto owner = std::make_unique<nf_native_mesh>();
    owner->backend = context->kernel;
    owner->source = source->operation.shape;
    owner->vertices.reserve(mesh->vertices.size());
    for (const auto& v : mesh->vertices)
      owner->vertices.push_back(
          {{v.position.x, v.position.y, v.position.z}, {v.normal.x, v.normal.y, v.normal.z}});
    owner->indices = std::move(mesh->indices);
    owner->faces = std::move(mesh->faces);
    *out = owner.release();
    return NF_NATIVE_OK;
#else
    return NF_NATIVE_UNAVAILABLE;
#endif
  });
}
uint32_t nf_native_mesh_query(const nf_native_mesh* mesh, nf_native_mesh_info* out) noexcept {
  if (out) *out = {};
  if (!mesh || !out) return NF_NATIVE_INVALID;
  return nf::native::guarded([&]() -> uint32_t {
#if NF_NATIVE_OCCT
    *out = {mesh->vertices.data(), mesh->indices.data(), mesh->vertices.size(),
            mesh->indices.size(), mesh->faces.size()};
    return NF_NATIVE_OK;
#else
    return NF_NATIVE_UNAVAILABLE;
#endif
  });
}
uint32_t nf_native_mesh_face_at(const nf_native_mesh* mesh, [[maybe_unused]] uint64_t index,
                                nf_native_mesh_face* out) noexcept {
  if (out) *out = {};
  if (!mesh || !out) return NF_NATIVE_INVALID;
  return nf::native::guarded([&]() -> uint32_t {
#if NF_NATIVE_OCCT
    if (index >= mesh->faces.size()) return NF_NATIVE_INVALID;
    const auto& face = mesh->faces[static_cast<size_t>(index)];
    if (!face.face || face.face->kind() != nf::kernel::EntityKind::kFace) return NF_NATIVE_FAILURE;
    *out = {reinterpret_cast<const nf_native_entity*>(face.face.get()), face.first_index,
            face.index_count};
    return NF_NATIVE_OK;
#else
    return NF_NATIVE_UNAVAILABLE;
#endif
  });
}
void nf_native_mesh_release(nf_native_mesh* mesh) noexcept { delete mesh; }
