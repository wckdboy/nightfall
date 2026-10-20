// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
// Private differential executable; never linked into the production kernel.
#include <iomanip>
#include <iostream>
#include <vector>
#include "nf/math/nurbs_surface.hpp"
int main() {
  std::cout << std::setprecision(17) << std::scientific;
  int p = 0, q = 0, weighted = 0;
  std::size_t nu = 0, nv = 0;
  double u = 0, v = 0;
  while (std::cin >> p >> q >> nu >> nv >> u >> v >> weighted) {
    std::vector<double> us(nu), vs(nv);
    for (double& x : us)
      if (!(std::cin >> x)) return 1;
    for (double& x : vs)
      if (!(std::cin >> x)) return 1;
    const auto ku = nf::math::KnotVector::make(std::move(us), p);
    const auto kv = nf::math::KnotVector::make(std::move(vs), q);
    if (!ku || !kv) return 2;
    const auto n = static_cast<std::size_t>(ku->control_point_count()) *
                   static_cast<std::size_t>(kv->control_point_count());
    std::vector<nf::math::Vec3> points(n);
    std::vector<double> weights(weighted ? n : 0);
    for (auto& point : points)
      if (!(std::cin >> point.x >> point.y >> point.z)) return 3;
    for (double& x : weights)
      if (!(std::cin >> x)) return 4;
    const auto s = nf::math::NurbsSurface::make(*ku, *kv, std::move(points), std::move(weights));
    if (!s) return 5;
    std::cout << s->rational();
    for (double x : s->weights()) std::cout << ' ' << x;
    const auto print = [](nf::math::Vec3 x) {
      std::cout << ' ' << x.x << ' ' << x.y << ' ' << x.z;
    };
    print(s->point(u, v));
    const auto d = s->derivatives(u, v, 3);
    for (int k = 0; k <= 3; ++k)
      for (int l = 0; l <= 3 - k; ++l) print(d.at(k, l));
    std::cout << '\n';
  }
  return std::cin.eof() ? 0 : 6;
}
