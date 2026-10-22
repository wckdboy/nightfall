# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
#
# The RFC 0014 study stack, prebuilt by third_party/build-studies.sh: Netgen (LGPL-2.1,
# linked dynamically only, WORKFLOW.md §8) meshes OCCT shapes, MFEM (BSD-3) solves. OCCT's
# Handle macro breaks MFEM's headers, so no translation unit includes both nf::netgen and
# nf::mfem headers.

set(NF_NETGEN_INSTALL_DIR "${PROJECT_SOURCE_DIR}/third_party/install/netgen-6.2.2604"
    CACHE PATH "Netgen install prefix (see third_party/build-studies.sh)")
set(NF_MFEM_INSTALL_DIR "${PROJECT_SOURCE_DIR}/third_party/install/mfem-4.10"
    CACHE PATH "MFEM install prefix (see third_party/build-studies.sh)")

foreach(part netgen mfem)
  string(TOUPPER "${part}" upper)
  if(NOT EXISTS "${NF_${upper}_INSTALL_DIR}/.nf-${part}-commit")
    message(FATAL_ERROR "${part} not found at ${NF_${upper}_INSTALL_DIR}. "
      "Run third_party/build-studies.sh (after build-occt.sh).")
  endif()
  file(READ "${NF_${upper}_INSTALL_DIR}/.nf-${part}-commit" ${part}_build)
  string(STRIP "${${part}_build}" ${part}_build)
endforeach()
if(NOT netgen_build MATCHES "^3ee489c7d58fdbc2a6708cca3cbaefaae506dc17-occt-")
  message(FATAL_ERROR "Netgen build receipt mismatch. Run third_party/build-studies.sh.")
endif()
if(NOT mfem_build STREQUAL "d964264cdb9a13e94a201b6c236c7721e0c8765f")
  message(FATAL_ERROR "MFEM build receipt mismatch. Run third_party/build-studies.sh.")
endif()
message(STATUS "Netgen 6.2.2604 from ${NF_NETGEN_INSTALL_DIR}; MFEM 4.10 from ${NF_MFEM_INSTALL_DIR}")

set(netgen_include "${NF_NETGEN_INSTALL_DIR}/include")
add_library(nf_netgen INTERFACE)
add_library(nf::netgen ALIAS nf_netgen)
target_include_directories(nf_netgen SYSTEM INTERFACE
  "${netgen_include}" "${netgen_include}/include" "${netgen_include}/meshing")
target_compile_definitions(nf_netgen INTERFACE OCCGEOMETRY NF_NETGEN_BUILD_ID="${netgen_build}")
target_link_libraries(nf_netgen INTERFACE
  "${NF_NETGEN_INSTALL_DIR}/lib/libnglib.dylib" "${NF_NETGEN_INSTALL_DIR}/lib/libngcore.dylib"
  nf::occt)

add_library(nf_mfem INTERFACE)
add_library(nf::mfem ALIAS nf_mfem)
target_include_directories(nf_mfem SYSTEM INTERFACE "${NF_MFEM_INSTALL_DIR}/include")
target_compile_definitions(nf_mfem INTERFACE NF_MFEM_BUILD_ID="${mfem_build}")
target_link_libraries(nf_mfem INTERFACE "${NF_MFEM_INSTALL_DIR}/lib/libmfem.dylib")

set(NF_STUDY_RPATH "${NF_OCCT_INSTALL_DIR}/lib;${NF_NETGEN_INSTALL_DIR}/lib;${NF_MFEM_INSTALL_DIR}/lib"
    CACHE INTERNAL "Runtime library paths for study binaries")

if(NF_BUILD_TESTS)
  # Two translation units, one per stack, joined by a plain mesh struct.
  add_library(nf_study_smoke_mesher STATIC tests/box_mesher.cpp)
  target_link_libraries(nf_study_smoke_mesher PUBLIC nf::netgen)
  # Netgen's headers are C++17; C++23 rejects their unique_ptr of an incomplete type.
  set_target_properties(nf_study_smoke_mesher PROPERTIES CXX_STANDARD 17)
  add_executable(study_smoke_tests tests/study_smoke_tests.cpp)
  target_link_libraries(study_smoke_tests PRIVATE nf_study_smoke_mesher nf::mfem nf::warnings
    Catch2::Catch2WithMain)
  set_target_properties(study_smoke_tests PROPERTIES BUILD_RPATH "${NF_STUDY_RPATH}")
  catch_discover_tests(study_smoke_tests TEST_PREFIX "studies." PROPERTIES LABELS "occt")
endif()
