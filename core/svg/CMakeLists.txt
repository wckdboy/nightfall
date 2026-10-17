# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0

# SVG ↔ exact NURBS (RFC 0004). Kernel-free, like libnf-math, so the app can import SVG
# without a round trip to the kernel service.
add_library(nf_svg STATIC src/number.cpp src/path.cpp src/export.cpp src/xml.cpp)
add_library(nf::svg ALIAS nf_svg)
target_include_directories(nf_svg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nf_svg PUBLIC nf::math PRIVATE nf::warnings)

if(NF_BUILD_TESTS)
  add_executable(nf_svg_tests tests/path_tests.cpp tests/export_tests.cpp
                              tests/xml_tests.cpp)
  target_link_libraries(nf_svg_tests PRIVATE nf::svg nf::warnings Catch2::Catch2WithMain)
  catch_discover_tests(nf_svg_tests TEST_PREFIX "svg." EXTRA_ARGS --rng-seed=20261016)
endif()
