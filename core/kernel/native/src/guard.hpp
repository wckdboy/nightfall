// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <exception>
#include "diagnostic.hpp"
namespace nf::native {
template <class F>
uint32_t guarded(F&& operation) noexcept {
  clear_error();
  try {
    return operation();
  } catch (const std::exception& error) {
    return failure("native_exception", error.what());
  } catch (...) {
    return failure("native_exception", "unknown native exception");
  }
}
}  // namespace nf::native
