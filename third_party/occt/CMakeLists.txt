# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
#
# OpenCASCADE, prebuilt by third_party/build-occt.sh, as the comparison/fallback kernel and
# STEP translator for the RFC 0001 bake-off. Enabled with -DNF_WITH_OCCT=ON.
# LGPL-2.1: linked dynamically only (WORKFLOW.md §8).

set(NF_OCCT_INSTALL_DIR "${PROJECT_SOURCE_DIR}/third_party/install/occt-7.9.3"
    CACHE PATH "OCCT install prefix (see third_party/build-occt.sh)")

find_package(OpenCASCADE 7.9.3 EXACT CONFIG REQUIRED
  PATHS "${NF_OCCT_INSTALL_DIR}" NO_DEFAULT_PATH)
message(STATUS "OpenCASCADE ${OpenCASCADE_VERSION} from ${NF_OCCT_INSTALL_DIR}")

# The toolkits Nightfall uses: modeling plus STEP.
add_library(nf_occt INTERFACE)
add_library(nf::occt ALIAS nf_occt)
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
