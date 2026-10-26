# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
#
# RFC 0014 study runner. Three translation-unit families with only standard types between
# them: Netgen (C++17; its headers reject C++23), OCCT through the kernel, and MFEM (OCCT's
# Handle macro breaks MFEM's headers).
add_library(nf_study_mesher STATIC src/netgen_mesher.cpp)
target_include_directories(nf_study_mesher PUBLIC src)
target_link_libraries(nf_study_mesher PRIVATE nf::netgen)
set_target_properties(nf_study_mesher PROPERTIES CXX_STANDARD 17)

add_library(nf_study STATIC src/geometry.cpp src/physics.cpp src/units.cpp src/study.cpp src/command.cpp)
target_include_directories(nf_study PUBLIC src PRIVATE "${PROJECT_SOURCE_DIR}/core/kernel/src")
# The runner evaluates documents; the evaluator lives with the kernel service.
target_link_libraries(nf_study PUBLIC nf_evaluator)
target_link_libraries(nf_study PUBLIC nf_study_mesher nf_kernel PRIVATE nf::mfem nf::occt nf::warnings)
# geometry.cpp includes OCCT headers only and physics.cpp MFEM headers only.

if(NF_BUILD_TESTS)
  add_executable(nf_study_tests tests/meshing_tests.cpp tests/physics_tests.cpp tests/study_tests.cpp)
  target_link_libraries(nf_study_tests PRIVATE nf_study nf_kernel nf::warnings Catch2::Catch2WithMain)
  target_compile_definitions(nf_study_tests PRIVATE
    NF_STUDY_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples/studies")
  set_target_properties(nf_study_tests PROPERTIES BUILD_RPATH "${NF_STUDY_RPATH}")
  catch_discover_tests(nf_study_tests TEST_PREFIX "study." PROPERTIES LABELS "occt")
endif()
