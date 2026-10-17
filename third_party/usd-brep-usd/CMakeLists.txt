# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
#
# usd-brep's USD modules (RFC 0002) built with plain CMake against the OpenUSD install from
# third_party/build-usd.sh: the only route for external geometry into SMLib
# (OCCT .brep -> USD BrepArray -> SM_API_USD -> SmBrep). Requires NF_WITH_SMLIB.
# Rule (RFC 0002): OCCT and OpenUSD never share a translation unit (OCCT's Handle macro
# breaks OpenUSD headers).

if(NOT NF_WITH_SMLIB)
  message(FATAL_ERROR "NF_WITH_USD requires NF_WITH_SMLIB")
endif()

set(NF_USD_INSTALL_DIR "${PROJECT_SOURCE_DIR}/third_party/install/usd-25.11"
    CACHE PATH "OpenUSD install prefix (see third_party/build-usd.sh)")
if(NOT EXISTS "${NF_USD_INSTALL_DIR}/pxrConfig.cmake")
  message(FATAL_ERROR
    "OpenUSD not found at ${NF_USD_INSTALL_DIR}. Run third_party/build-usd.sh first.")
endif()
find_package(pxr CONFIG REQUIRED PATHS "${NF_USD_INSTALL_DIR}" NO_DEFAULT_PATH)
message(STATUS "OpenUSD ${PXR_VERSION} from ${NF_USD_INSTALL_DIR}")

set(_src "${NF_SMLIB_SOURCE_DIR}/source")
# Third-party code: our nf::warnings are deliberately not applied.
set(_opts -Wno-deprecated -Wno-unused-variable -Wno-unused-but-set-variable
          -Wno-inconsistent-missing-override -Wno-overloaded-virtual)
set(_usd usd usdGeom usdShade usdUtils sdf tf gf vt plug ar kind js)

# Plugin directory for usd-brep's omniSolid schema (set PXR_PLUGINPATH_NAME to it at runtime).
set(NF_OMNISOLID_PLUGIN_DIR "${_src}/schema/omniSolid/resources" CACHE INTERNAL "")

function(nf_usdbrep_lib name kind)
  file(GLOB _files CONFIGURE_DEPENDS "${_src}/${name}/src/*.cpp")
  add_library(${name} ${kind} ${_files})
  set_target_properties(${name} PROPERTIES CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
  target_include_directories(${name} SYSTEM PUBLIC "${_src}/${name}/inc")
  target_compile_options(${name} PRIVATE ${_opts})
  target_link_libraries(${name} PUBLIC ${_usd})
endfunction()

nf_usdbrep_lib(BREP_USD_DATA SHARED)
target_compile_definitions(BREP_USD_DATA PRIVATE USDBREP_ENABLE_EXPORTS)
nf_usdbrep_lib(BREP_STAGING STATIC)
target_link_libraries(BREP_STAGING PUBLIC BREP_USD_DATA)
nf_usdbrep_lib(OCCT_BREP_IMPORT STATIC)
target_link_libraries(OCCT_BREP_IMPORT PUBLIC BREP_STAGING BREP_USD_DATA)
nf_usdbrep_lib(BREP_SM_USD SHARED)
target_compile_definitions(BREP_SM_USD PRIVATE SMU_ENABLE_EXPORTS)
target_link_libraries(BREP_SM_USD PUBLIC smlib BREP_USD_DATA)
nf_usdbrep_lib(SM_API_USD SHARED)
target_compile_definitions(SM_API_USD PRIVATE SM_API_USD_EXPORTS)
target_link_libraries(SM_API_USD PUBLIC smlib sm_api BREP_SM_USD BREP_USD_DATA)

# Everything the bake-off needs to bring an OCCT .brep into SMLib.
add_library(nf_usdbrep INTERFACE)
add_library(nf::usdbrep ALIAS nf_usdbrep)
target_link_libraries(nf_usdbrep INTERFACE OCCT_BREP_IMPORT SM_API_USD)

if(NF_BUILD_TESTS AND NF_WITH_OCCT)
  add_executable(usdbrep_pipeline_tests tests/pipeline_tests.cpp tests/occt_side.cpp)
  set_target_properties(usdbrep_pipeline_tests PROPERTIES CXX_STANDARD 17)
  target_link_libraries(usdbrep_pipeline_tests PRIVATE nf::usdbrep nf::occt Catch2::Catch2WithMain)
  catch_discover_tests(usdbrep_pipeline_tests TEST_PREFIX "usdbrep." PROPERTIES
    LABELS "usd"
    ENVIRONMENT "PXR_PLUGINPATH_NAME=${NF_OMNISOLID_PLUGIN_DIR}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endif()
