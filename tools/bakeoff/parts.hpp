// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "kernel.hpp"

namespace nf::bakeoff {

/// What a correct result looks like. Unset fields are not checked (e.g. face counts that
/// legitimately differ between kernels).
struct Expectation {
  std::optional<double> volume;  ///< mm³, analytic
  std::optional<double> area;    ///< mm², analytic
};

/// A reference part: an operation script plus its expected result.
struct Part {
  std::string name;
  std::string category;  ///< "primitive", "boolean", "fillet", "shell", …
  std::function<Body(Kernel&)> build;
  Expectation expect;
};

/// The reference corpus (RFC 0001), growing to 20 parts in #17.
[[nodiscard]] std::vector<Part> reference_parts();

}  // namespace nf::bakeoff
