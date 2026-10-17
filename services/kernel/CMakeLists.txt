# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0

# Private kernel-service components (RFCs 0003, 0005). No kernel dependency in the app.
if(NF_BUILD_TESTS)
  add_executable(nf_kernel_cache_tests tests/cache_tests.cpp)
  target_include_directories(nf_kernel_cache_tests PRIVATE src)
  target_link_libraries(nf_kernel_cache_tests PRIVATE nf::ir nf::warnings Catch2::Catch2WithMain)
  if(NF_WITH_OCCT)
    target_sources(nf_kernel_cache_tests PRIVATE tests/cache_occt_tests.cpp)
    target_link_libraries(nf_kernel_cache_tests PRIVATE nf::occt)
    set_target_properties(nf_kernel_cache_tests PROPERTIES BUILD_RPATH "${NF_OCCT_INSTALL_DIR}/lib")
  endif()
  catch_discover_tests(nf_kernel_cache_tests TEST_PREFIX "kernel-cache." EXTRA_ARGS --rng-seed=20261017)
endif()
