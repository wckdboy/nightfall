# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0

# The Rust solvers' plain-C boundary: the sketch constraint solver (RFC 0012) and the assembly
# joint solver (RFC 0013), built by Cargo (locked, offline) into ONE static library. Two Rust
# static libraries cannot share a binary without duplicate standard-library symbols.
find_program(NF_CARGO cargo REQUIRED)
set(nf_ffi_profile release)
set(nf_ffi_cargo_profile --release)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(nf_ffi_profile debug)
  set(nf_ffi_cargo_profile)
endif()
set(nf_ffi_target_dir ${CMAKE_CURRENT_BINARY_DIR}/cargo)
set(nf_ffi_lib ${nf_ffi_target_dir}/${nf_ffi_profile}/libnightfall_ffi.a)
file(GLOB_RECURSE nf_ffi_sources CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.rs ${PROJECT_SOURCE_DIR}/core/sketch/rust/src/*.rs
  ${PROJECT_SOURCE_DIR}/core/assembly/rust/src/*.rs
  ${PROJECT_SOURCE_DIR}/third_party/solidcraft-sketch/*.rs)
add_custom_command(
  OUTPUT ${nf_ffi_lib}
  COMMAND ${CMAKE_COMMAND} -E env CARGO_TARGET_DIR=${nf_ffi_target_dir}
    MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
    ${NF_CARGO} build -p nightfall-ffi --lib ${nf_ffi_cargo_profile} --locked --offline
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  DEPENDS ${nf_ffi_sources} ${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml
    ${PROJECT_SOURCE_DIR}/core/sketch/rust/Cargo.toml
    ${PROJECT_SOURCE_DIR}/core/assembly/rust/Cargo.toml ${PROJECT_SOURCE_DIR}/Cargo.lock
  COMMENT "Building the Rust solvers (Cargo, ${nf_ffi_profile})"
  VERBATIM)
add_custom_target(nf_ffi_cargo DEPENDS ${nf_ffi_lib})
add_library(nf_ffi STATIC IMPORTED GLOBAL)
set_target_properties(nf_ffi PROPERTIES IMPORTED_LOCATION ${nf_ffi_lib}
  INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_dependencies(nf_ffi nf_ffi_cargo)
add_library(nf::ffi ALIAS nf_ffi)

if(NF_BUILD_TESTS)
  add_executable(nf_sketch_c_smoke tests/sketch_c_smoke.c)
  target_link_libraries(nf_sketch_c_smoke PRIVATE nf::ffi)
  target_compile_options(nf_sketch_c_smoke PRIVATE -Wall -Wextra -Wpedantic -Werror)
  set_target_properties(nf_sketch_c_smoke PROPERTIES C_STANDARD 17 C_EXTENSIONS OFF)
  add_test(NAME sketch.c_smoke COMMAND nf_sketch_c_smoke)
  add_executable(nf_assembly_c_smoke tests/assembly_c_smoke.c)
  target_link_libraries(nf_assembly_c_smoke PRIVATE nf::ffi)
  target_compile_options(nf_assembly_c_smoke PRIVATE -Wall -Wextra -Wpedantic -Werror)
  set_target_properties(nf_assembly_c_smoke PROPERTIES C_STANDARD 17 C_EXTENSIONS OFF)
  add_test(NAME assembly.c_smoke COMMAND nf_assembly_c_smoke)
endif()
