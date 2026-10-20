// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <string>
#include <string_view>
#include "nf_native.h"
namespace nf::native {
inline thread_local std::string error_code, error_message;
inline void clear_error() noexcept {
  error_code.clear();
  error_message.clear();
}
inline uint32_t failure(std::string_view code, std::string_view message) noexcept {
  try {
    error_code = code;
    error_message = message;
  } catch (...) {
    clear_error();  // Allocation failure still returns the complete failure status.
  }
  return NF_NATIVE_FAILURE;
}
}  // namespace nf::native
