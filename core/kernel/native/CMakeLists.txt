# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
function(nf_native_adapter name occt directory)
  add_library(${name} SHARED src/context.cpp src/extrusion.cpp src/transform.cpp)
  target_include_directories(${name} PUBLIC include)
  target_compile_definitions(${name} PRIVATE NF_NATIVE_OCCT=$<BOOL:${occt}>)
  target_link_libraries(${name} PRIVATE nf::warnings)
  if(APPLE)
    target_link_options(${name} PRIVATE
      "LINKER:-exported_symbol,_nf_native_context_create"
      "LINKER:-exported_symbol,_nf_native_context_info"
      "LINKER:-exported_symbol,_nf_native_context_release"
      "LINKER:-exported_symbol,_nf_native_extrude"
      "LINKER:-exported_symbol,_nf_native_transform"
      "LINKER:-exported_symbol,_nf_native_result_history"
      "LINKER:-exported_symbol,_nf_native_result_history_entity"
      "LINKER:-exported_symbol,_nf_native_result_query"
      "LINKER:-exported_symbol,_nf_native_result_generated"
      "LINKER:-exported_symbol,_nf_native_entities_same"
      "LINKER:-exported_symbol,_nf_native_result_release")
  endif()
  set_target_properties(${name} PROPERTIES OUTPUT_NAME nf_native_adapter
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${directory}"
    BUILD_RPATH "${NF_OCCT_INSTALL_DIR}/lib")
  if(occt)
    set_target_properties(nf_kernel nf_math PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(${name} PRIVATE nf::kernel)
  endif()
  if(NF_BUILD_TESTS)
    add_executable(${name}_tests tests/context_tests.cpp tests/extrusion_tests.cpp tests/transform_tests.cpp)
    target_include_directories(${name}_tests PRIVATE src)
    target_compile_definitions(${name}_tests PRIVATE NF_EXPECT_OCCT=$<BOOL:${occt}>)
    target_link_libraries(${name}_tests PRIVATE ${name} nf::warnings Catch2::Catch2WithMain)
    if(occt)
      target_link_libraries(${name}_tests PRIVATE nf::kernel)
    endif()
    catch_discover_tests(${name}_tests TEST_PREFIX "native-abi.${name}.")
    add_executable(${name}_c_smoke tests/c_smoke.c)
    target_compile_definitions(${name}_c_smoke PRIVATE NF_EXPECT_OCCT=$<BOOL:${occt}>)
    target_link_libraries(${name}_c_smoke PRIVATE ${name})
    target_compile_options(${name}_c_smoke PRIVATE -Wall -Wextra -Wpedantic -Werror)
    set_target_properties(${name}_c_smoke PROPERTIES C_STANDARD 17 C_EXTENSIONS OFF)
    add_test(NAME native-abi.${name}.c COMMAND ${name}_c_smoke)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    find_program(NF_CARGO cargo REQUIRED)
    add_test(NAME native-abi.${name}.rust COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_rust.py ${CMAKE_SOURCE_DIR} ${NF_CARGO}
      $<TARGET_FILE_DIR:${name}> $<BOOL:${occt}> ${CMAKE_BINARY_DIR}/rust-${name})
  endif()
endfunction()
nf_native_adapter(nf_native_adapter "${NF_WITH_OCCT}" linked)
if(NF_BUILD_TESTS AND NF_WITH_OCCT)
  nf_native_adapter(nf_native_unavailable OFF unavailable)
endif()
