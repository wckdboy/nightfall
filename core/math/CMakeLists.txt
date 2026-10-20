# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0

# libnf-math: Nightfall's own, kernel-free NURBS math core (RFC 0004). Used by both the app
# and the kernel service; never depends on a kernel.
add_library(nf_math STATIC src/knot_vector.cpp src/basis.cpp src/nurbs_curve.cpp
                            src/constructors.cpp src/refine.cpp src/query.cpp
                            src/intersect.cpp src/fit.cpp src/arrangement.cpp src/facade.cpp
                            src/nurbs_surface.cpp src/expression.cpp
                            src/equation.cpp)
add_library(nf::math ALIAS nf_math)
target_include_directories(nf_math PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(nf_math PUBLIC cxx_std_23)
target_link_libraries(nf_math PRIVATE nf::warnings)

if(NF_BUILD_TESTS)
  add_executable(nf_math_tests
    tests/tolerance_tests.cpp
    tests/vec_tests.cpp
    tests/basis_tests.cpp
    tests/curve_tests.cpp
    tests/constructors_tests.cpp
    tests/refine_tests.cpp
    tests/query_tests.cpp
    tests/intersect_tests.cpp
    tests/fit_tests.cpp
    tests/arrangement_tests.cpp
    tests/region_tests.cpp
    tests/shared_curve_tests.cpp
    tests/surface_tests.cpp
    tests/expression_tests.cpp
    tests/equation_tests.cpp
    tests/facade_tests.cpp)
  target_link_libraries(nf_math_tests PRIVATE nf::math nf::warnings Catch2::Catch2WithMain)
  # Fixed seed so property-test failures reproduce exactly; run the binary directly with
  # --rng-seed time to explore new samples.
  catch_discover_tests(nf_math_tests TEST_PREFIX "math." EXTRA_ARGS --rng-seed=20261016)
  add_executable(nf_rust_basis_oracle tests/rust_basis_oracle.cpp)
  target_link_libraries(nf_rust_basis_oracle PRIVATE nf::math nf::warnings)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME math.rust-basis-differential
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/rust_basis_differential.py
      $<TARGET_FILE:nf_rust_basis_oracle> ${CMAKE_SOURCE_DIR})
  add_executable(nf_rust_curve_oracle tests/rust_curve_oracle.cpp)
  target_link_libraries(nf_rust_curve_oracle PRIVATE nf::math nf::warnings)
  add_test(NAME math.rust-curve-differential
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/rust_curve_differential.py
      $<TARGET_FILE:nf_rust_curve_oracle> ${CMAKE_SOURCE_DIR})
  add_executable(nf_rust_curve_bench tests/rust_curve_bench.cpp)
  target_link_libraries(nf_rust_curve_bench PRIVATE nf::math nf::warnings)
  add_executable(nf_rust_surface_oracle tests/rust_surface_oracle.cpp)
  target_link_libraries(nf_rust_surface_oracle PRIVATE nf::math nf::warnings)
  add_test(NAME math.rust-surface-differential
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/rust_surface_differential.py
      $<TARGET_FILE:nf_rust_surface_oracle> ${CMAKE_SOURCE_DIR})
endif()

# Benchmark for the Phase 1 gate (cubic evaluation ≤ 50 ns); built, not run by ctest.
add_executable(nf_math_bench bench/eval_bench.cpp)
target_link_libraries(nf_math_bench PRIVATE nf::math nf::warnings)
