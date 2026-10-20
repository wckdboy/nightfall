// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "context.hpp"
#include <cmath>
#include "guard.hpp"
#include "nf_native.h"
#if NF_NATIVE_OCCT
#include <array>
#endif

uint32_t nf_native_last_error(nf_native_error_info* out) noexcept {
  if (!out || reinterpret_cast<uintptr_t>(out) % alignof(nf_native_error_info))
    return NF_NATIVE_INVALID;
  *out = {reinterpret_cast<const uint8_t*>(nf::native::error_code.data()),
          nf::native::error_code.size(),
          reinterpret_cast<const uint8_t*>(nf::native::error_message.data()),
          nf::native::error_message.size()};
  return NF_NATIVE_OK;
}
uint32_t nf_native_context_create(uint32_t abi, double linear, double angular,
                                  nf_native_context** out) noexcept {
  nf::native::clear_error();
  if (!out) return NF_NATIVE_INVALID;
  *out = nullptr;
  if (abi != NF_NATIVE_ABI_VERSION) return NF_NATIVE_ABI_MISMATCH;
  if (!std::isfinite(linear) || !std::isfinite(angular) || linear <= 0 || angular <= 0 ||
      angular >= 1)
    return NF_NATIVE_INVALID;
  return nf::native::guarded([&]() -> uint32_t {
#if NF_NATIVE_OCCT
    auto kernel = nf::kernel::make_occt_kernel({linear, angular});
    if (!kernel) return NF_NATIVE_INVALID;
    *out = new nf_native_context{std::move(*kernel)};
    return NF_NATIVE_OK;
#else
    return NF_NATIVE_UNAVAILABLE;
#endif
  });
}
uint32_t nf_native_context_info(const nf_native_context* context, nf_native_info* out) noexcept {
  if (out) *out = {};
  if (!out || !context) return NF_NATIVE_INVALID;
  return nf::native::guarded([&]() -> uint32_t {
#if NF_NATIVE_OCCT
    const auto c = context->kernel->capabilities();
    const std::array<bool, 9> flags{c.extrude, c.rigid_transform, c.booleans, c.revolve, c.mirror,
                                    c.chamfer, c.fillet,          c.loft,     c.sweep};
    uint32_t bits = 0;
    for (size_t i = 0; i < flags.size(); ++i) bits |= static_cast<uint32_t>(flags[i]) << i;
    const auto identity = context->kernel->identity();
    *out = {NF_NATIVE_ABI_VERSION, bits, reinterpret_cast<const uint8_t*>(identity.data()),
            identity.size()};
    return NF_NATIVE_OK;
#else
    return NF_NATIVE_UNAVAILABLE;
#endif
  });
}
void nf_native_context_release(nf_native_context* context) noexcept {
  (void)nf::native::guarded([&]() -> uint32_t {
    delete context;
    return NF_NATIVE_OK;
  });
}
