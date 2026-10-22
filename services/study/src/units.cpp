// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "units.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>

namespace nf::study {
namespace {
struct Unit {
  Dimension dimension;
  std::string_view name;
  double scale, offset;  // value = number * scale + offset
};
constexpr std::array kUnits{
    Unit{Dimension::kForce, "N", 1, 0},
    Unit{Dimension::kForce, "kN", 1e3, 0},
    Unit{Dimension::kStress, "Pa", 1e-6, 0},
    Unit{Dimension::kStress, "kPa", 1e-3, 0},
    Unit{Dimension::kStress, "MPa", 1, 0},
    Unit{Dimension::kStress, "GPa", 1e3, 0},
    Unit{Dimension::kTemperature, "K", 1, 0},
    Unit{Dimension::kTemperature, "degC", 1, 273.15},
    Unit{Dimension::kHeatFlux, "W/m^2", 1, 0},
    Unit{Dimension::kHeatFlux, "kW/m^2", 1e3, 0},
    Unit{Dimension::kFilmCoefficient, "W/m^2/K", 1, 0},
    Unit{Dimension::kConductivity, "W/m/K", 1, 0},
};
constexpr std::string_view name(Dimension d) {
  switch (d) {
    case Dimension::kForce:
      return "a force (N, kN)";
    case Dimension::kStress:
      return "a pressure or stress (Pa, kPa, MPa, GPa)";
    case Dimension::kTemperature:
      return "a temperature (K, degC)";
    case Dimension::kHeatFlux:
      return "a heat flux (W/m^2, kW/m^2)";
    case Dimension::kFilmCoefficient:
      return "a film coefficient (W/m^2/K)";
    case Dimension::kConductivity:
      return "a conductivity (W/m/K)";
  }
  return "a value";
}
}  // namespace

std::expected<double, std::string> parse_value(std::string_view text, Dimension dimension) {
  const auto space = text.find(' ');
  const auto expected = [&] {
    return std::unexpected("\"" + std::string(text) + "\" is not " + std::string(name(dimension)));
  };
  if (space == std::string_view::npos) return expected();
  const auto number = text.substr(0, space), unit = text.substr(space + 1);
  const std::string copy(number);
  char* end = nullptr;
  const double value = std::strtod(copy.c_str(), &end);
  if (copy.empty() || end != copy.c_str() + copy.size() || !std::isfinite(value)) return expected();
  for (const auto& u : kUnits)
    if (u.dimension == dimension && u.name == unit) return value * u.scale + u.offset;
  return expected();
}
}  // namespace nf::study
