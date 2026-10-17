// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// STEP -> OCCT .brep, for kernels that import through usd-brep's .brep path (RFC 0002).
// Implemented in step_bridge.cpp, an OCCT-only translation unit: OCCT and OpenUSD headers
// must never meet in one TU.

#pragma once

#include <string>

namespace nf::bakeoff {

/// Reads `step_path` with OCCT and writes it as an OCCT .brep. Returns false and sets
/// `error` on failure.
bool step_to_brep(const std::string& step_path, const std::string& brep_path, std::string& error);

}  // namespace nf::bakeoff
