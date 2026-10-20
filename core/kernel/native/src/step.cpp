// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include <cstddef>
#include <limits>
#include <string>
#include "guard.hpp"
#include "result.hpp"
struct nf_native_step {
  std::string bytes;
};
namespace {
template <class T>
bool aligned(const T* pointer) {
  return pointer && reinterpret_cast<uintptr_t>(pointer) % alignof(T) == 0;
}
}  // namespace
uint32_t nf_native_export_step(nf_native_context* context, const nf_native_result* const* sources,
                               uint64_t source_count, nf_native_step** out) noexcept {
  nf::native::clear_error();
  if (!aligned(out)) return NF_NATIVE_INVALID;
  *out = nullptr;
  if (!aligned(context) || !aligned(sources) || !source_count ||
      source_count > std::numeric_limits<size_t>::max() / sizeof(*sources) ||
      source_count >
          static_cast<uint64_t>(std::numeric_limits<std::ptrdiff_t>::max()) / sizeof(*sources))
    return NF_NATIVE_INVALID;
  return nf::native::guarded([&]() -> uint32_t {
#if NF_NATIVE_OCCT
    if (!context->kernel) return NF_NATIVE_INVALID;
    std::vector<nf::kernel::ShapePtr> inputs;
    const auto count = static_cast<size_t>(source_count);
    if (count > inputs.max_size()) return NF_NATIVE_INVALID;
    // Validate the COMPLETE owner list before any geometry access or allocation.
    for (size_t i = 0; i < count; ++i)
      if (!aligned(sources[i]) || sources[i]->backend != context->kernel ||
          !sources[i]->operation.shape)
        return NF_NATIVE_INVALID;
    inputs.reserve(count);
    for (size_t i = 0; i < count; ++i) inputs.push_back(sources[i]->operation.shape);
    auto encoded = context->kernel->export_step(inputs);
    if (!encoded) return nf::native::failure(encoded.error().code, encoded.error().message);
    if (encoded->empty() ||
        encoded->size() > static_cast<size_t>(std::numeric_limits<std::ptrdiff_t>::max()))
      return NF_NATIVE_FAILURE;
    auto owner = std::make_unique<nf_native_step>();
    owner->bytes = std::move(*encoded);
    *out = owner.release();
    return NF_NATIVE_OK;
#else
    return NF_NATIVE_UNAVAILABLE;
#endif
  });
}
uint32_t nf_native_step_query(const nf_native_step* step, nf_native_step_info* out) noexcept {
  if (!aligned(out)) return NF_NATIVE_INVALID;
  *out = {};
  if (!aligned(step)) return NF_NATIVE_INVALID;
  *out = {reinterpret_cast<const uint8_t*>(step->bytes.data()),
          static_cast<uint64_t>(step->bytes.size())};
  return NF_NATIVE_OK;
}
void nf_native_step_release(nf_native_step* step) noexcept { delete step; }
