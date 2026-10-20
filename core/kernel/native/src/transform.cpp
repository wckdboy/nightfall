// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "guard.hpp"
#include "result.hpp"
uint32_t nf_native_transform(nf_native_context* context, const nf_native_result* source,
                             const nf_native_rigid_transform* input,
                             nf_native_result** out) noexcept {
  nf::native::clear_error();
  if (out) *out = nullptr;
  if (!context || !source || !input || !out ||
      reinterpret_cast<uintptr_t>(input) % alignof(nf_native_rigid_transform))
    return NF_NATIVE_INVALID;
  return nf::native::guarded([&]() -> uint32_t {
#if NF_NATIVE_OCCT
    if (source->backend != context->kernel) return NF_NATIVE_INVALID;
    const auto vec = [](const double* p) { return nf::math::Vec3{p[0], p[1], p[2]}; };
    auto result = context->kernel->transform(source->operation.shape,
                                             {vec(input->translation), vec(input->axis_origin),
                                              vec(input->axis_direction), input->angle});
    if (!result) return nf::native::failure(result.error().code, result.error().message);
    *out = new nf_native_result{context->kernel, std::move(*result)};
    return NF_NATIVE_OK;
#else
    return NF_NATIVE_UNAVAILABLE;
#endif
  });
}
uint32_t nf_native_result_history(const nf_native_result* result, [[maybe_unused]] uint64_t index,
                                  nf_native_history* out) noexcept {
  if (out) *out = {};
  if (!result || !out) return NF_NATIVE_INVALID;
  return nf::native::guarded([&]() -> uint32_t {
#if NF_NATIVE_OCCT
    if (index >= result->operation.history.size()) return NF_NATIVE_INVALID;
    const auto& row = result->operation.history[static_cast<size_t>(index)];
    if (!row.before) return NF_NATIVE_FAILURE;
    *out = {reinterpret_cast<const nf_native_entity*>(row.before.get()), row.modified.size(),
            row.generated.size(), static_cast<uint32_t>(row.before->kind()) + 1u,
            static_cast<uint32_t>(row.deleted)};
    return NF_NATIVE_OK;
#else
    return NF_NATIVE_UNAVAILABLE;
#endif
  });
}
uint32_t nf_native_result_history_entity(const nf_native_result* result,
                                         [[maybe_unused]] uint64_t history, uint32_t list,
                                         [[maybe_unused]] uint64_t index,
                                         nf_native_entity_view* out) noexcept {
  if (out) *out = {};
  if (!result || !out || list > 1) return NF_NATIVE_INVALID;
  return nf::native::guarded([&]() -> uint32_t {
#if NF_NATIVE_OCCT
    if (history >= result->operation.history.size()) return NF_NATIVE_INVALID;
    const auto& row = result->operation.history[static_cast<size_t>(history)];
    const auto& entities = list == 0 ? row.modified : row.generated;
    if (index >= entities.size()) return NF_NATIVE_INVALID;
    const auto& entity = entities[static_cast<size_t>(index)];
    if (!entity) return NF_NATIVE_FAILURE;
    *out = {reinterpret_cast<const nf_native_entity*>(entity.get()),
            static_cast<uint32_t>(entity->kind()) + 1u};
    return NF_NATIVE_OK;
#else
    return NF_NATIVE_UNAVAILABLE;
#endif
  });
}
