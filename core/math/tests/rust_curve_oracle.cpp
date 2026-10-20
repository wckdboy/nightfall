// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
// Test-only differential protocol; never linked into the kernel or client.
#include <array>
#include <iomanip>
#include <iostream>
#include <type_traits>
#include <vector>

#include "nf/math/nurbs_curve.hpp"

template <int D>
bool probe() {
  int degree = 0, weighted = 0;
  std::size_t count = 0;
  double u = 0.0;
  if (!(std::cin >> degree >> count >> u >> weighted)) return false;
  std::vector<double> values(count);
  for (double& value : values)
    if (!(std::cin >> value)) return false;
  auto knots = nf::math::KnotVector::make(std::move(values), degree);
  if (!knots) return false;
  using Point = std::conditional_t<D == 2, nf::math::Vec2, nf::math::Vec3>;
  std::vector<Point> points(static_cast<std::size_t>(knots->control_point_count()));
  for (auto& point : points) {
    if (!(std::cin >> point.x >> point.y)) return false;
    if constexpr (D == 3) {
      if (!(std::cin >> point.z)) return false;
    }
  }
  std::vector<double> weights;
  if (weighted != 0) {
    weights.resize(points.size());
    for (double& value : weights)
      if (!(std::cin >> value)) return false;
  }
  auto curve = nf::math::NurbsCurve<Point>::make(*knots, std::move(points), std::move(weights));
  if (!curve) return false;
  std::array<Point, 13> derivatives{};
  curve->derivatives(u, 12, derivatives);
  std::cout << curve->rational();
  for (double value : curve->weights()) std::cout << ' ' << value;
  const auto print = [](Point p) {
    std::cout << ' ' << p.x << ' ' << p.y;
    if constexpr (D == 3) std::cout << ' ' << p.z;
  };
  print(curve->point(u));
  for (auto value : derivatives) print(value);
  std::cout << '\n';
  return true;
}
int main() {
  std::cout << std::setprecision(17) << std::scientific;
  int dimension = 0;
  while (std::cin >> dimension) {
    if (dimension == 2) {
      if (!probe<2>()) return 1;
    } else if (dimension == 3) {
      if (!probe<3>()) return 2;
    } else
      return 3;
  }
  return std::cin.eof() ? 0 : 4;
}
