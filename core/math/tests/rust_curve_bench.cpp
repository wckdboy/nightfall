// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
// Equivalent inputs to rust/tests/support/curve_bench.rs; not a ctest timing assertion.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "nf/math/nurbs_curve.hpp"

int main() {
  const auto knots = nf::math::KnotVector::uniform_clamped(3, 20);
  std::vector<nf::math::Vec3> points;
  for (int i = 0; i < 20; ++i) points.push_back({i * 0.25, i % 7 - 3.0, i * i % 11 * 0.125});
  std::vector<double> us;
  for (std::uint64_t i = 0; i < 1'000'000; ++i) {
    us.push_back(static_cast<double>((i * 16807 + 7) % 1'000'003) / 1'000'003.0);
  }
  volatile double sink = 0;
  for (bool rational : {false, true}) {
    std::vector<double> weights;
    if (rational)
      for (int i = 0; i < 20; ++i) weights.push_back((i % 7 + 1) * 0.25);
    const auto curve = nf::math::NurbsCurve3::make(knots, points, weights).value();
    std::array<double, 5> runs{};
    double checksum = 0;
    for (double& run : runs) {
      const auto start = std::chrono::steady_clock::now();
      double sum = 0;
      for (double u : us) {
        const auto p = curve.point(u);
        sum += p.x + p.y + p.z;
      }
      run = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start)
                .count() /
            static_cast<double>(us.size());
      sink = sink + sum;
      checksum += sum;
    }
    std::ranges::sort(runs);
    std::printf("C++ rational=%s: median %.2f ns (range %.2f–%.2f), checksum %.17e\n",
                rational ? "true" : "false", runs[2], runs[0], runs[4], checksum);
  }
}
