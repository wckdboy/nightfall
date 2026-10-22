// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// RFC 0014 physical values: "<number> <unit>" strings checked against a field's dimension and
// converted to the runner's units. A bare number is refused, so 10 never silently means
// newtons.
#pragma once

#include <expected>
#include <string>
#include <string_view>

namespace nf::study {
enum class Dimension { kForce, kStress, kTemperature, kHeatFlux, kFilmCoefficient, kConductivity };
/// The value in N, MPa, K, W/m², W/(m² K) or W/(m K), by dimension.
std::expected<double, std::string> parse_value(std::string_view text, Dimension dimension);
}  // namespace nf::study
