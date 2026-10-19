# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
#
# OpenCASCADE, prebuilt by third_party/build-occt.sh, as the comparison/fallback kernel and
# STEP translator for the RFC 0001 bake-off. Enabled with -DNF_WITH_OCCT=ON.
# LGPL-2.1: linked dynamically only (WORKFLOW.md §8).

set(NF_OCCT_INSTALL_DIR "${PROJECT_SOURCE_DIR}/third_party/install/occt-7.9.3"
    CACHE PATH "OCCT install prefix (see third_party/build-occt.sh)")

if(NOT EXISTS "${NF_OCCT_INSTALL_DIR}/lib/cmake/opencascade")
  message(FATAL_ERROR
    "OpenCASCADE not found at ${NF_OCCT_INSTALL_DIR}. Run third_party/build-occt.sh first.")
endif()
find_package(OpenCASCADE 7.9.3 EXACT CONFIG REQUIRED
  PATHS "${NF_OCCT_INSTALL_DIR}" NO_DEFAULT_PATH)
message(STATUS "OpenCASCADE ${OpenCASCADE_VERSION} from ${NF_OCCT_INSTALL_DIR}")
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/patches/0001-align-incremental-allocations.patch" patch_hash)
if(NOT EXISTS "${NF_OCCT_INSTALL_DIR}/.nf-occt-commit")
  message(FATAL_ERROR "OCCT build receipt missing. Run third_party/build-occt.sh.")
endif()
file(READ "${NF_OCCT_INSTALL_DIR}/.nf-occt-commit" occt_build)
string(STRIP "${occt_build}" occt_build)
if(NOT occt_build MATCHES "^a016080bf6738d6aeae020badee4e888ad1540a5(-asan)?-inc-align-${patch_hash}$")
  message(FATAL_ERROR "OCCT allocator patch receipt mismatch. Run third_party/build-occt.sh.")
endif()

# The toolkits Nightfall uses: modeling plus STEP.
add_library(nf_occt INTERFACE)
add_library(nf::occt ALIAS nf_occt)
target_compile_definitions(nf_occt INTERFACE NF_OCCT_BUILD_ID="${occt_build}")
target_link_libraries(nf_occt INTERFACE
  TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo TKPrim TKBO TKBool
  TKFillet TKOffset TKShHealing TKMesh TKDE TKXSBase TKDESTEP)

if(NF_BUILD_TESTS)
  add_executable(occt_smoke_tests tests/occt_smoke_tests.cpp)
  target_link_libraries(occt_smoke_tests PRIVATE nf::occt nf::warnings Catch2::Catch2WithMain)
  set_target_properties(occt_smoke_tests PROPERTIES BUILD_RPATH "${NF_OCCT_INSTALL_DIR}/lib")
  catch_discover_tests(occt_smoke_tests TEST_PREFIX "occt." PROPERTIES LABELS "occt"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endif()
