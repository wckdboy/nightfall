# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
#
# Kernel bake-off harness (RFC 0001). The runner and parts build everywhere and are tested
# with a fake kernel; kernel drivers (#16, #21) build only when their kernel is enabled.

add_library(nf_bakeoff STATIC runner.cpp parts.cpp)
target_include_directories(nf_bakeoff PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nf_bakeoff PUBLIC nf::math PRIVATE nf::warnings)

if(NF_WITH_OCCT)
  target_sources(nf_bakeoff PRIVATE occt_kernel.cpp)
  target_link_libraries(nf_bakeoff PUBLIC nf::occt)
  target_compile_definitions(nf_bakeoff PUBLIC NF_BAKEOFF_WITH_OCCT=1)
endif()

if(NF_WITH_SMLIB)
  file(STRINGS "${NF_SMLIB_SOURCE_DIR}/VERSION" _smlib_version LIMIT_COUNT 1)
  target_sources(nf_bakeoff PRIVATE smlib_kernel.cpp)
  target_link_libraries(nf_bakeoff PUBLIC nf::sm_api)
  target_compile_definitions(nf_bakeoff PUBLIC NF_BAKEOFF_WITH_SMLIB=1
                             PRIVATE NF_SMLIB_VERSION="${_smlib_version}")
endif()

add_executable(nf_bakeoff_cli main.cpp)
set_target_properties(nf_bakeoff_cli PROPERTIES OUTPUT_NAME nf_bakeoff)
target_link_libraries(nf_bakeoff_cli PRIVATE nf_bakeoff nf::warnings)

if(NF_BUILD_TESTS)
  add_executable(nf_bakeoff_tests tests/runner_tests.cpp)
  target_link_libraries(nf_bakeoff_tests PRIVATE nf_bakeoff nf::warnings Catch2::Catch2WithMain)
  catch_discover_tests(nf_bakeoff_tests TEST_PREFIX "bakeoff.")

  if(NF_WITH_OCCT)
    add_executable(nf_bakeoff_occt_tests tests/occt_parts_tests.cpp)
    target_link_libraries(nf_bakeoff_occt_tests PRIVATE nf_bakeoff nf::warnings
                          Catch2::Catch2WithMain)
    catch_discover_tests(nf_bakeoff_occt_tests TEST_PREFIX "bakeoff.occt."
                         PROPERTIES LABELS "occt")
  endif()

  if(NF_WITH_SMLIB)
    add_executable(nf_bakeoff_smlib_tests tests/smlib_parts_tests.cpp)
    target_link_libraries(nf_bakeoff_smlib_tests PRIVATE nf_bakeoff nf::warnings
                          Catch2::Catch2WithMain)
    catch_discover_tests(nf_bakeoff_smlib_tests TEST_PREFIX "bakeoff.smlib."
                         PROPERTIES LABELS "smlib")
  endif()
endif()
