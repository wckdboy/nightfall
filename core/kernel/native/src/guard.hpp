// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "nf_native.h"
namespace nf::native {
template <class F>
uint32_t guarded(F&& operation) noexcept {
  try {
    return operation();
  } catch (...) {
    return NF_NATIVE_FAILURE;
  }
}
}  // namespace nf::native
