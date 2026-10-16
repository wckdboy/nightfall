# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
#
# Builds the SMLib kernel and its SM_API wrapper from a usd-brep checkout with plain
# CMake. Upstream builds with NVIDIA's repo_man/premake/Packman toolchain, whose macOS
# premake package is not publicly downloadable; the kernel itself needs none of it.
#
# Not built: USD, Python bindings, TBB (optional upstream via SM_USE_TBB), Hydra.
# Evaluation only (RFC 0001); enabled with -DNF_WITH_SMLIB=ON.

set(NF_SMLIB_SOURCE_DIR "${PROJECT_SOURCE_DIR}/third_party/src/usd-brep"
    CACHE PATH "usd-brep checkout (see third_party/fetch-smlib.sh)")

if(NOT EXISTS "${NF_SMLIB_SOURCE_DIR}/source/SMLib/inc")
  message(FATAL_ERROR
    "usd-brep not found at ${NF_SMLIB_SOURCE_DIR}. Run third_party/fetch-smlib.sh first.")
endif()

set(_src "${NF_SMLIB_SOURCE_DIR}")

# Upstream's macOS flags (premake5-utils.lua), minus -ansi and the universal arch.
# Third-party code: our nf::warnings are deliberately not applied.
set(_smlib_options
  -Wall -Wno-missing-braces -Wno-inconsistent-missing-override -Wno-overloaded-virtual
  -Wno-unused-but-set-variable -Wno-unused-variable -Wno-parentheses-equality)

file(GLOB _smlib_sources CONFIGURE_DEPENDS "${_src}/source/SMLib/src/*.cpp")
add_library(smlib SHARED ${_smlib_sources})
add_library(nf::smlib ALIAS smlib)
set_target_properties(smlib PROPERTIES OUTPUT_NAME SMLib CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_include_directories(smlib SYSTEM PUBLIC "${_src}/source/SMLib/inc")
target_compile_options(smlib PRIVATE ${_smlib_options})

file(GLOB _smapi_sources CONFIGURE_DEPENDS "${_src}/source/SM_API/src/*.cpp")
add_library(sm_api SHARED ${_smapi_sources})
add_library(nf::sm_api ALIAS sm_api)
set_target_properties(sm_api PROPERTIES OUTPUT_NAME SM_API CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_include_directories(sm_api SYSTEM PUBLIC "${_src}/source/SM_API/inc")
target_compile_definitions(sm_api PRIVATE SM_API_EXPORTS)
target_compile_options(sm_api PRIVATE ${_smlib_options})
target_link_libraries(sm_api PUBLIC smlib)

if(NF_BUILD_TESTS)
  # Upstream's own SM_API test suite, run unchanged from the checkout root (it reads
  # fixtures by relative path).
  file(GLOB _smapi_test_sources CONFIGURE_DEPENDS "${_src}/tests/SM_API_test/src/*.cpp")
  add_executable(smlib_api_tests
    ${_smapi_test_sources}
    "${_src}/Examples/SM_API/boolean_bounds.cpp"
    "${_src}/Examples/SMLib/step_derivatives.cpp")
  set_target_properties(smlib_api_tests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
  target_include_directories(smlib_api_tests PRIVATE
    "${_src}/tests/SM_API_test/inc" "${_src}/Examples/SM_API" "${_src}/Examples/SMLib")
  target_compile_definitions(smlib_api_tests PRIVATE SM_API_TEST_STANDALONE)
  target_compile_options(smlib_api_tests PRIVATE ${_smlib_options} -Wno-write-strings)
  target_link_libraries(smlib_api_tests PRIVATE sm_api smlib)

  add_test(NAME smlib.SM_API_test COMMAND smlib_api_tests WORKING_DIRECTORY "${_src}")
  set_tests_properties(smlib.SM_API_test PROPERTIES
    LABELS "smlib;slow"
    PASS_REGULAR_EXPRESSION "RESULT: PASS"
    FAIL_REGULAR_EXPRESSION "RESULT: FAIL"
    TIMEOUT 600)
endif()
