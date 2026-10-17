// swift-tools-version: 6.0
// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// Swift access to libnf-math (RFC 0004): CNightfallMath compiles core/math as C++23, and
// NightfallMath wraps its facade (nf/math/facade.hpp) in Swift types via C++ interop. The
// app and Nightfall Script build on NightfallMath; CMake stays the build for C++.

import PackageDescription

let package = Package(
  name: "Nightfall",
  platforms: [.macOS(.v15)],
  products: [.library(name: "NightfallMath", targets: ["NightfallMath"])],
  targets: [
    .target(
      name: "CNightfallMath",
      path: "core/math",
      sources: ["src"],
      publicHeadersPath: "include"),
    .target(
      name: "NightfallMath",
      dependencies: ["CNightfallMath"],
      path: "swift/NightfallMath",
      swiftSettings: [.interoperabilityMode(.Cxx)]),
    .testTarget(
      name: "NightfallMathTests",
      dependencies: ["NightfallMath"],
      path: "swift/NightfallMathTests",
      swiftSettings: [.interoperabilityMode(.Cxx)]),
  ],
  cxxLanguageStandard: .cxx2b)
