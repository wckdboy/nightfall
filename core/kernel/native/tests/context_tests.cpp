// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string_view>
#include "guard.hpp"
#include "nf_native.h"

TEST_CASE("adapter contains standard and nonstandard native exceptions") {
  bool called = false;
  REQUIRE(nf::native::guarded([&]() -> uint32_t {
            called = true;
            throw std::runtime_error("test");
          }) == NF_NATIVE_FAILURE);
  REQUIRE(called);
  REQUIRE(nf::native::guarded([]() -> uint32_t { throw 42; }) == NF_NATIVE_FAILURE);
  REQUIRE(nf::native::guarded([]() -> uint32_t { return NF_NATIVE_OK; }) == NF_NATIVE_OK);
  struct ThrowOnCopy {
    ThrowOnCopy() = default;
    ThrowOnCopy(const ThrowOnCopy&) { throw 42; }
    uint32_t operator()() const { return NF_NATIVE_OK; }
  } operation;
  REQUIRE(nf::native::guarded(operation) == NF_NATIVE_OK);
}

TEST_CASE("context ABI layout and null boundaries") {
  STATIC_REQUIRE(sizeof(uint32_t) == 4);
  STATIC_REQUIRE(sizeof(nf_native_info) == 8 + sizeof(void*) + sizeof(size_t));
  STATIC_REQUIRE(offsetof(nf_native_info, identity) == 8);
  STATIC_REQUIRE(offsetof(nf_native_info, identity_bytes) == 8 + sizeof(void*));
  REQUIRE(nf_native_context_create(1, 1e-6, 1e-9, nullptr) == NF_NATIVE_INVALID);
  nf_native_info info{99, 99, nullptr, 99};
  REQUIRE(nf_native_context_info(nullptr, &info) == NF_NATIVE_INVALID);
  REQUIRE(info.abi_version == 0);
  REQUIRE(info.capabilities == 0);
  REQUIRE(info.identity == nullptr);
  REQUIRE(info.identity_bytes == 0);
  REQUIRE(nf_native_context_info(nullptr, nullptr) == NF_NATIVE_INVALID);
  nf_native_context_release(nullptr);
}
TEST_CASE("native diagnostic ABI handles malformed inputs and unavailable backend") {
  STATIC_REQUIRE(sizeof(nf_native_error_info) == 4 * sizeof(size_t));
  STATIC_REQUIRE(offsetof(nf_native_error_info, message) == 2 * sizeof(size_t));
  nf_native_error_info error{};
  REQUIRE(nf_native_last_error(nullptr) == NF_NATIVE_INVALID);
  REQUIRE(nf_native_last_error(&error) == NF_NATIVE_OK);
  nf_native_context* context = nullptr;
  const auto created = nf_native_context_create(1, 1e-6, 1e-9, &context);
  if constexpr (!NF_EXPECT_OCCT) {
    REQUIRE(created == NF_NATIVE_UNAVAILABLE);
    return;
  }
  REQUIRE(created == NF_NATIVE_OK);
  nf_native_extrusion input{};
  nf_native_result* result = nullptr;
  REQUIRE(nf_native_extrude(context, &input, &result) == NF_NATIVE_INVALID);
  REQUIRE(nf_native_last_error(&error) == NF_NATIVE_OK);
  REQUIRE(error.code_bytes == 0);
  REQUIRE(error.message_bytes == 0);
  nf_native_context_release(context);
}
TEST_CASE("failed creation publishes no owner") {
  nf_native_context* context = nullptr;
  REQUIRE(nf_native_context_create(99, 1e-6, 1e-9, &context) == NF_NATIVE_ABI_MISMATCH);
  REQUIRE(context == nullptr);
  for (double value : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN()}) {
    REQUIRE(nf_native_context_create(1, value, 1e-9, &context) == NF_NATIVE_INVALID);
    REQUIRE(context == nullptr);
    REQUIRE(nf_native_context_create(1, 1e-6, value, &context) == NF_NATIVE_INVALID);
    REQUIRE(context == nullptr);
  }
  REQUIRE(nf_native_context_create(1, 1e-6, 1.0, &context) == NF_NATIVE_INVALID);
  if constexpr (NF_EXPECT_OCCT) {
    REQUIRE(nf_native_context_create(1, 1e-100, 1e-9, &context) == NF_NATIVE_INVALID);
    REQUIRE(context == nullptr);
    REQUIRE(nf_native_context_create(1, 1e-6, 1e-100, &context) == NF_NATIVE_INVALID);
    REQUIRE(context == nullptr);
  }
}
TEST_CASE("backend context retains a complete immutable identity") {
  for (int iteration = 0; iteration != 128; ++iteration) {
    nf_native_context* context = nullptr;
    const auto status = nf_native_context_create(NF_NATIVE_ABI_VERSION, 1e-6, 1e-9, &context);
    if constexpr (NF_EXPECT_OCCT) {
      REQUIRE(status == NF_NATIVE_OK);
      REQUIRE(context != nullptr);
      nf_native_info info{};
      REQUIRE(nf_native_context_info(context, &info) == NF_NATIVE_OK);
      REQUIRE(info.abi_version == 1);
      REQUIRE(info.capabilities == 511);
      REQUIRE(std::string_view(reinterpret_cast<const char*>(info.identity), info.identity_bytes)
                  .starts_with("occt/7.9.3;build="));
      nf_native_context* independent = nullptr;
      REQUIRE(nf_native_context_create(1, 1e-6, 1e-9, &independent) == NF_NATIVE_OK);
      nf_native_context_release(independent);
      REQUIRE(std::string_view(reinterpret_cast<const char*>(info.identity), info.identity_bytes)
                  .find("linear=") != std::string_view::npos);
    } else {
      REQUIRE(status == NF_NATIVE_UNAVAILABLE);
      REQUIRE(context == nullptr);
    }
    nf_native_context_release(context);
  }
}
