# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
#
# SPIKE (RFC 0002): usd-brep's USD modules built with plain CMake against the OpenUSD
# install from third_party/build-usd.sh. Requires NF_WITH_SMLIB and NF_WITH_OCCT.

set(NF_USD_INSTALL_DIR "${PROJECT_SOURCE_DIR}/third_party/install/usd-25.11"
    CACHE PATH "OpenUSD install prefix (see third_party/build-usd.sh)")
find_package(pxr CONFIG REQUIRED PATHS "${NF_USD_INSTALL_DIR}" NO_DEFAULT_PATH)

set(_src "${NF_SMLIB_SOURCE_DIR}/source")
set(_opts -Wno-deprecated -Wno-unused-variable -Wno-unused-but-set-variable
          -Wno-inconsistent-missing-override -Wno-overloaded-virtual)
set(_usd usd usdGeom usdShade usdUtils sdf tf gf vt plug ar kind js)

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

# STEP -> OCCT -> .brep -> USD BrepArray -> SmBrep, volume compared with OCCT's.
add_executable(nf_usd_spike spike.cpp occt_side.cpp)
set_target_properties(nf_usd_spike PROPERTIES CXX_STANDARD 17)
target_link_libraries(nf_usd_spike PRIVATE OCCT_BREP_IMPORT SM_API_USD nf::occt)
