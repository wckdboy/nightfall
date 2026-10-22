# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28)

project(Nightfall
  VERSION 0.0.1
  DESCRIPTION "Open-source, AI-native parametric CAD"
  LANGUAGES CXX)

option(NF_BUILD_TESTS "Build unit tests" ON)
option(NF_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(NF_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(NF_WITH_SMLIB "Build the SMLib kernel from third_party/src/usd-brep (RFC 0001 evaluation)" OFF)
option(NF_WITH_OCCT "Use OpenCASCADE built by third_party/build-occt.sh (RFC 0001 evaluation)" OFF)
option(NF_WITH_USD "Use OpenUSD from third_party/build-usd.sh and usd-brep's USD modules (RFC 0002)" OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Warnings applied to every Nightfall target (not third-party code).
add_library(nf_warnings INTERFACE)
target_compile_options(nf_warnings INTERFACE
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
  -Wnon-virtual-dtor -Wold-style-cast -Wimplicit-fallthrough
  $<$<BOOL:${NF_WARNINGS_AS_ERRORS}>:-Werror>)
add_library(nf::warnings ALIAS nf_warnings)

if(NF_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
endif()

if(NF_BUILD_TESTS)
  enable_language(C)
  enable_testing()
  include(FetchContent)
  # Pinned by commit (v3.16.1). See third_party/README.md.
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        5010924c4d8e912e64fd64a11819eecacf572c06
    GIT_SHALLOW    FALSE
    SYSTEM)
  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  include(Catch)
endif()

add_subdirectory(core)

if(NF_WITH_SMLIB)
  add_subdirectory(third_party/smlib)
endif()
if(NF_WITH_OCCT)
  add_subdirectory(third_party/occt)
endif()
if(NF_WITH_USD)
  add_subdirectory(third_party/usd-brep-usd)
endif()

add_subdirectory(tools/bakeoff)
add_subdirectory(services/kernel)
if(NF_WITH_OCCT)
  add_subdirectory(tools/cli)
  add_subdirectory(agent/evals)
endif()
