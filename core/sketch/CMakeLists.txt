# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0

# The sketch constraint solver (RFC 0012): Rust, built by Cargo (locked, offline) into a static
# library with a plain-C header, linked into the C++ evaluator.
find_program(NF_CARGO cargo REQUIRED)
set(nf_sketch_profile release)
set(nf_sketch_cargo_profile --release)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(nf_sketch_profile debug)
  set(nf_sketch_cargo_profile)
endif()
set(nf_sketch_target_dir ${CMAKE_CURRENT_BINARY_DIR}/cargo)
set(nf_sketch_lib ${nf_sketch_target_dir}/${nf_sketch_profile}/libnightfall_sketch_ffi.a)
file(GLOB_RECURSE nf_sketch_sources CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/rust/src/*.rs ${CMAKE_CURRENT_SOURCE_DIR}/ffi/src/*.rs
  ${PROJECT_SOURCE_DIR}/third_party/solidcraft-sketch/*.rs)
add_custom_command(
  OUTPUT ${nf_sketch_lib}
  COMMAND ${CMAKE_COMMAND} -E env CARGO_TARGET_DIR=${nf_sketch_target_dir}
    MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
    ${NF_CARGO} build -p nightfall-sketch-ffi --lib ${nf_sketch_cargo_profile} --locked --offline
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  DEPENDS ${nf_sketch_sources} ${CMAKE_CURRENT_SOURCE_DIR}/ffi/Cargo.toml
    ${CMAKE_CURRENT_SOURCE_DIR}/rust/Cargo.toml ${PROJECT_SOURCE_DIR}/Cargo.lock
  COMMENT "Building the sketch solver (Cargo, ${nf_sketch_profile})"
  VERBATIM)
add_custom_target(nf_sketch_cargo DEPENDS ${nf_sketch_lib})
add_library(nf_sketch STATIC IMPORTED GLOBAL)
set_target_properties(nf_sketch PROPERTIES IMPORTED_LOCATION ${nf_sketch_lib}
  INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/ffi/include)
add_dependencies(nf_sketch nf_sketch_cargo)
add_library(nf::sketch ALIAS nf_sketch)

if(NF_BUILD_TESTS)
  add_executable(nf_sketch_c_smoke ffi/tests/c_smoke.c)
  target_link_libraries(nf_sketch_c_smoke PRIVATE nf::sketch)
  target_compile_options(nf_sketch_c_smoke PRIVATE -Wall -Wextra -Wpedantic -Werror)
  set_target_properties(nf_sketch_c_smoke PROPERTIES C_STANDARD 17 C_EXTENSIONS OFF)
  add_test(NAME sketch.c_smoke COMMAND nf_sketch_c_smoke)
endif()
