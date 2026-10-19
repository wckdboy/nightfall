# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_executable(Nightfall MACOSX_BUNDLE NightfallApp.swift PartProgram.swift PreviewDocument.swift PreviewView.swift KernelClient.swift DisplayMesh.swift MetalRenderer.swift OrbitView.swift "${PROJECT_SOURCE_DIR}/services/kernel/swift/KernelProtocol.swift")
target_compile_options(Nightfall PRIVATE -parse-as-library -swift-version 6 -strict-concurrency=complete -warnings-as-errors -target "${nf_swift_arch}-apple-macosx${nf_swift_deployment}" -module-cache-path "${CMAKE_CURRENT_BINARY_DIR}/module-cache")
set_target_properties(Nightfall PROPERTIES MACOSX_BUNDLE_GUI_IDENTIFIER "dev.nightfall.Nightfall" MACOSX_BUNDLE_BUNDLE_NAME "Nightfall" MACOSX_BUNDLE_BUNDLE_VERSION 3 MACOSX_BUNDLE_SHORT_VERSION_STRING "0.0.3")
add_dependencies(Nightfall nf_kernel_service)
add_custom_command(TARGET Nightfall POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_directory "${PROJECT_SOURCE_DIR}/examples/parts" "$<TARGET_BUNDLE_DIR:Nightfall>/Contents/Resources/Examples"
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/embed_service.py" "$<TARGET_BUNDLE_DIR:Nightfall>" "$<TARGET_BUNDLE_DIR:nf_kernel_service>" "${NF_OCCT_INSTALL_DIR}"
  VERBATIM)
file(GLOB examples CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/examples/parts/*.nightfall")
set_property(TARGET Nightfall APPEND PROPERTY LINK_DEPENDS "${PROJECT_SOURCE_DIR}/tools/embed_service.py" "$<TARGET_FILE:nf_kernel_service>" ${examples})
