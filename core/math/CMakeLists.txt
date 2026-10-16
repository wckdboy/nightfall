# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0

# libnf-math: Nightfall's NURBS math API. Header-only until the NLib wrapper lands
# (Phase 1, after RFC 0001 decides the kernel).
add_library(nf_math INTERFACE)
add_library(nf::math ALIAS nf_math)
target_include_directories(nf_math INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(nf_math INTERFACE cxx_std_23)

if(NF_BUILD_TESTS)
  add_executable(nf_math_tests
    tests/tolerance_tests.cpp
    tests/vec_tests.cpp)
  target_link_libraries(nf_math_tests PRIVATE nf::math nf::warnings Catch2::Catch2WithMain)
  catch_discover_tests(nf_math_tests TEST_PREFIX "math.")
endif()
