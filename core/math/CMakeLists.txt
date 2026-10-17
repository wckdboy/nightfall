# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0

# libnf-math: Nightfall's own, kernel-free NURBS math core (RFC 0004). Used by both the app
# and the kernel service; never depends on a kernel.
add_library(nf_math STATIC src/knot_vector.cpp src/basis.cpp src/nurbs_curve.cpp
                            src/constructors.cpp src/refine.cpp src/query.cpp
                            src/intersect.cpp src/fit.cpp src/arrangement.cpp)
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
    tests/region_tests.cpp)
  target_link_libraries(nf_math_tests PRIVATE nf::math nf::warnings Catch2::Catch2WithMain)
  # Fixed seed so property-test failures reproduce exactly; run the binary directly with
  # --rng-seed time to explore new samples.
  catch_discover_tests(nf_math_tests TEST_PREFIX "math." EXTRA_ARGS --rng-seed=20261016)
endif()
