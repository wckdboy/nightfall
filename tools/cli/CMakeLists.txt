# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
add_executable(nf main.cpp session.cpp mcp.cpp)
target_link_libraries(nf PRIVATE nf_evaluator nf::warnings)
set_target_properties(nf PROPERTIES BUILD_RPATH "${NF_OCCT_INSTALL_DIR}/lib")
if(NF_BUILD_TESTS)
  add_executable(nf_cli_tests tests/build_tests.cpp tests/mcp_tests.cpp)
  add_dependencies(nf_cli_tests nf)
  target_include_directories(nf_cli_tests PRIVATE ${PROJECT_SOURCE_DIR}/core/kernel/src)
  target_compile_definitions(nf_cli_tests PRIVATE NF_CLI_PATH="${CMAKE_CURRENT_BINARY_DIR}/nf"
    NF_REFERENCE_PARTS_DIR="${PROJECT_SOURCE_DIR}/examples/parts")
  target_link_libraries(nf_cli_tests PRIVATE nf_evaluator nf::warnings Catch2::Catch2WithMain)
  set_target_properties(nf_cli_tests PROPERTIES BUILD_RPATH "${NF_OCCT_INSTALL_DIR}/lib")
  catch_discover_tests(nf_cli_tests TEST_PREFIX "cli." EXTRA_ARGS --rng-seed=20261018
    PROPERTIES LABELS "occt")
endif()
