# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
add_executable(nf main.cpp session.cpp mcp.cpp kernel_serve.cpp)
target_link_libraries(nf PRIVATE nf_evaluator nf::warnings)
set_target_properties(nf PROPERTIES BUILD_RPATH "${NF_OCCT_INSTALL_DIR}/lib")
if(NF_WITH_STUDIES)
  # RFC 0014: `nf study run`.
  target_link_libraries(nf PRIVATE nf_study)
  target_compile_definitions(nf PRIVATE NF_WITH_STUDIES)
  set_target_properties(nf PROPERTIES BUILD_RPATH "${NF_STUDY_RPATH}")
  if(NF_BUILD_TESTS)
    add_test(NAME cli.study_run COMMAND nf study run
      "${PROJECT_SOURCE_DIR}/examples/studies/thermal_slab.nightfall" conduction)
    set_tests_properties(cli.study_run PROPERTIES LABELS "occt"
      PASS_REGULAR_EXPRESSION "\"passed\":true")
    add_test(NAME cli.study_unknown COMMAND nf study run
      "${PROJECT_SOURCE_DIR}/examples/studies/thermal_slab.nightfall" missing)
    set_tests_properties(cli.study_unknown PROPERTIES LABELS "occt" WILL_FAIL TRUE)
    # #317: the same study object on a STEP part, faces picked by points.
    add_test(NAME cli.study_solve COMMAND nf study solve
      "${PROJECT_SOURCE_DIR}/examples/studies/cantilever.step"
      "${PROJECT_SOURCE_DIR}/examples/studies/cantilever.study.json")
    set_tests_properties(cli.study_solve PROPERTIES LABELS "occt"
      PASS_REGULAR_EXPRESSION "\"passed\":true")
  endif()
endif()
if(NF_BUILD_TESTS)
  add_executable(nf_cli_tests tests/build_tests.cpp tests/mcp_tests.cpp tests/kernel_serve_tests.cpp)
  add_dependencies(nf_cli_tests nf)
  target_include_directories(nf_cli_tests PRIVATE ${PROJECT_SOURCE_DIR}/core/kernel/src)
  target_compile_definitions(nf_cli_tests PRIVATE NF_CLI_PATH="${CMAKE_CURRENT_BINARY_DIR}/nf"
    NF_REFERENCE_PARTS_DIR="${PROJECT_SOURCE_DIR}/examples/parts")
  target_link_libraries(nf_cli_tests PRIVATE nf_evaluator nf::warnings Catch2::Catch2WithMain)
  set_target_properties(nf_cli_tests PROPERTIES BUILD_RPATH "${NF_OCCT_INSTALL_DIR}/lib")
  catch_discover_tests(nf_cli_tests TEST_PREFIX "cli." EXTRA_ARGS --rng-seed=20261018
    PROPERTIES LABELS "occt")
  # RFC 0013 URDF export, checked by forward kinematics independent of the engine.
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(NAME cli.urdf_export COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/urdf_tests.py" "$<TARGET_FILE:nf>"
    "${PROJECT_SOURCE_DIR}/examples/assemblies")
  set_tests_properties(cli.urdf_export PROPERTIES LABELS "occt")
  if(NF_WITH_STUDIES)
    target_compile_definitions(nf_cli_tests PRIVATE NF_WITH_STUDIES
      NF_STUDY_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples/studies")
  endif()
endif()
