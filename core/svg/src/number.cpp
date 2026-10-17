// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "number.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <string>

#include <xlocale.h>

namespace nf::svg::detail {

ScannedNumber scan_number(std::string_view s, std::size_t pos) {
  std::size_t i = pos;
  const auto digits = [&] {
    const std::size_t from = i;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
    return i > from;
  };
  if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
  bool mantissa = digits();
  if (i < s.size() && s[i] == '.') {
    ++i;
    mantissa = digits() || mantissa;
  }
  if (!mantissa) return {0.0, pos, NumberError::kMissing};
  if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
    const std::size_t mark = i++;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
    if (!digits()) i = mark;
  }
  // std::from_chars(double) needs macOS 26; strtod_l in the C locale reads the scanned token.
  static const locale_t c_locale = newlocale(LC_ALL_MASK, "C", nullptr);
  const std::string token(s.substr(pos, i - pos));
  char* stop = nullptr;
  const double value = strtod_l(token.c_str(), &stop, c_locale);
  if (stop != token.c_str() + token.size() || !std::isfinite(value)) {
    return {0.0, pos, NumberError::kOutOfRange};
  }
  return {value, i, NumberError::kNone};
}

std::string format_number(double x) {
  if (x == 0.0) x = 0.0;
  std::array<char, 32> buf{};
  const auto r = std::to_chars(buf.data(), buf.data() + buf.size(), x);
  return std::string(buf.data(), r.ptr);
}

}  // namespace nf::svg::detail
