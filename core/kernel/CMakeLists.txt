# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0

# The kernel layer on OpenCASCADE (RFC 0001). Runs only in the kernel service (RFC 0003);
# the app never links it. OCCT and OpenUSD headers never share a translation unit.
add_library(nf_kernel STATIC src/occt_geom.cpp src/snapshot.cpp src/occt_kernel.cpp)
add_library(nf::kernel ALIAS nf_kernel)
target_include_directories(nf_kernel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(nf_kernel PUBLIC nf::math nf::occt PRIVATE nf::warnings)

if(NF_BUILD_TESTS)
  add_executable(nf_kernel_tests tests/occt_geom_tests.cpp tests/snapshot_tests.cpp tests/operations_tests.cpp tests/boolean_tests.cpp tests/revolution_tests.cpp tests/mirror_tests.cpp tests/face_plane_tests.cpp tests/step_tests.cpp tests/circular_profile_tests.cpp tests/planar_mass_tests.cpp)
  target_include_directories(nf_kernel_tests PRIVATE src)
  target_compile_definitions(nf_kernel_tests PRIVATE NF_RELEASE_PERFORMANCE=$<AND:$<CONFIG:Release>,$<NOT:$<BOOL:${NF_SANITIZE}>>>)
  target_sources(nf_kernel_tests PRIVATE tests/chamfer_tests.cpp)
  target_sources(nf_kernel_tests PRIVATE tests/fillet_tests.cpp)
  target_sources(nf_kernel_tests PRIVATE tests/mesh_tests.cpp)
  target_sources(nf_kernel_tests PRIVATE tests/surface_mass_tests.cpp)
  target_link_libraries(nf_kernel_tests PRIVATE nf::kernel nf::warnings Catch2::Catch2WithMain)
  set_target_properties(nf_kernel_tests PROPERTIES BUILD_RPATH "${NF_OCCT_INSTALL_DIR}/lib")
  catch_discover_tests(nf_kernel_tests TEST_PREFIX "kernel." EXTRA_ARGS --rng-seed=20261016
                       PROPERTIES LABELS "occt")
endif()
