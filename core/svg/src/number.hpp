// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// Internal: the SVG 2 number grammar, shared by path data, transforms and lengths.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace nf::svg::detail {

enum class NumberError { kNone, kMissing, kOutOfRange };

struct ScannedNumber {
  double value = 0.0;
  std::size_t end = 0;  // one past the token
  NumberError error = NumberError::kNone;
};

/// Scans sign? (digits ('.' digits?)? | '.' digits) exponent? at s[pos], exactly to the
/// grammar ("1e" is the number 1 followed by "e"), converted in the C locale.
[[nodiscard]] ScannedNumber scan_number(std::string_view s, std::size_t pos);

/// The shortest text that reads back to the same double; never "-0".
[[nodiscard]] std::string format_number(double x);

}  // namespace nf::svg::detail
