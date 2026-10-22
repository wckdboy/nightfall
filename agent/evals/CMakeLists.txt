# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
# The agent evaluation suite (RFC 0011, gate G1). Only the deterministic reference runner is a
# test: it drives the real `nf mcp` server and the measurement pipeline without a model.
if(NF_BUILD_TESTS)
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME evals.reference
      COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/run.py
        --nf $<TARGET_FILE:nf> --parts ${PROJECT_SOURCE_DIR}/examples/parts
        --out ${CMAKE_BINARY_DIR}/evals-reference --runner reference)
    set_tests_properties(evals.reference PROPERTIES LABELS "occt" TIMEOUT 600)
  endif()
endif()
