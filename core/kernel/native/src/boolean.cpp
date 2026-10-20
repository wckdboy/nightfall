// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include <cstddef>
#include <limits>
#include "guard.hpp"
#include "result.hpp"
namespace {
template <class T>
bool aligned(const T* pointer) {
  return pointer && reinterpret_cast<uintptr_t>(pointer) % alignof(T) == 0;
}
}  // namespace
uint32_t nf_native_boolean(nf_native_context* context, const nf_native_result* target,
                           const nf_native_result* const* tools, uint64_t tool_count, uint32_t mode,
                           nf_native_result** out) noexcept {
  if (!aligned(out)) return NF_NATIVE_INVALID;
  *out = nullptr;
  if (!aligned(context) || !aligned(target) || !aligned(tools) || !tool_count ||
      mode > NF_NATIVE_INTERSECT ||
      tool_count > std::numeric_limits<size_t>::max() / sizeof(*tools) ||
      tool_count >
          static_cast<uint64_t>(std::numeric_limits<std::ptrdiff_t>::max()) / sizeof(*tools))
    return NF_NATIVE_INVALID;
  return nf::native::guarded([&]() -> uint32_t {
#if NF_NATIVE_OCCT
    if (!context->kernel || target->backend != context->kernel || !target->operation.shape)
      return NF_NATIVE_INVALID;
    std::vector<nf::kernel::ShapePtr> inputs;
    const auto count = static_cast<size_t>(tool_count);
    if (count > inputs.max_size()) return NF_NATIVE_INVALID;
    // Validate every operand before allocating or entering geometry, including mixed lists.
    for (size_t i = 0; i < count; ++i)
      if (!aligned(tools[i]) || tools[i]->backend != context->kernel || !tools[i]->operation.shape)
        return NF_NATIVE_INVALID;
    inputs.reserve(count);
    for (size_t i = 0; i < count; ++i) inputs.push_back(tools[i]->operation.shape);
    nf::kernel::BooleanOp op;
    switch (mode) {
      case NF_NATIVE_UNION:
        op = nf::kernel::BooleanOp::kUnion;
        break;
      case NF_NATIVE_SUBTRACT:
        op = nf::kernel::BooleanOp::kSubtract;
        break;
      default:
        op = nf::kernel::BooleanOp::kIntersect;
        break;
    }
    auto result = context->kernel->boolean_operation(target->operation.shape, inputs, op);
    if (!result) return NF_NATIVE_FAILURE;
    *out = new nf_native_result{context->kernel, std::move(*result)};
    return NF_NATIVE_OK;
#else
    return NF_NATIVE_UNAVAILABLE;
#endif
  });
}
