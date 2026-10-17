# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0

add_subdirectory(math)
add_subdirectory(svg)
add_subdirectory(ir)
if(NF_WITH_OCCT)
  add_subdirectory(kernel)
endif()
