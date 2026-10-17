# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0

# The feature IR (RFC 0005): the part document, its validation, migrations and hashing.
# Kernel-free, used by both the app and the kernel service.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS schema/part-v1.schema.json)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/schema/part-v1.schema.json" NF_PART_SCHEMA)
configure_file(src/schema_data.hpp.in schema_data.hpp @ONLY)
add_library(nf_ir STATIC src/json.cpp src/document.cpp src/schema.cpp)
add_library(nf::ir ALIAS nf_ir)
target_include_directories(nf_ir PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(nf_ir PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(nf_ir PRIVATE nf::warnings)

if(NF_BUILD_TESTS)
  add_executable(nf_ir_tests tests/json_tests.cpp tests/document_tests.cpp tests/schema_tests.cpp)
  target_include_directories(nf_ir_tests PRIVATE src)
  target_compile_definitions(nf_ir_tests PRIVATE NF_IR_CORPUS="${CMAKE_CURRENT_SOURCE_DIR}/tests/schema-corpus.json")
  target_link_libraries(nf_ir_tests PRIVATE nf::ir nf::warnings Catch2::Catch2WithMain)
  catch_discover_tests(nf_ir_tests TEST_PREFIX "ir." EXTRA_ARGS --rng-seed=20261017)
endif()
