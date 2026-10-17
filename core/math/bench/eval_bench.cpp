// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// Phase 1 gate: cubic NURBS point evaluation ≤ 50 ns (RFC 0004). Run a release build:
//   cmake --preset release && cmake --build build/release --target nf_math_bench &&
//   build/release/core/math/nf_math_bench

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

#include "nf/math/nurbs_curve.hpp"

using namespace nf::math;

namespace {

// Median of five runs of `calls` evaluations at random parameters, in ns per call.
double ns_per_point(const NurbsCurve3& c, int calls) {
  std::mt19937 rng(7);
  std::uniform_real_distribution<double> param(0.0, 1.0);
  std::vector<double> us(static_cast<std::size_t>(calls));
  for (double& u : us) u = param(rng);
  std::vector<double> runs;
  volatile double sink = 0;
  for (int r = 0; r < 5; ++r) {
    const auto t0 = std::chrono::steady_clock::now();
    double acc = 0;
    for (const double u : us) acc += c.point(u).x;
    const auto t1 = std::chrono::steady_clock::now();
    sink = sink + acc;
    runs.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() / calls);
  }
  std::ranges::sort(runs);
  return runs[2];
}

}  // namespace

int main() {
  std::mt19937 rng(1);
  std::uniform_real_distribution<double> coord(-10, 10), weight(0.5, 2);
  const KnotVector kv = KnotVector::uniform_clamped(3, 20);
  std::vector<Vec3> pts;
  std::vector<double> ws;
  for (int i = 0; i < kv.control_point_count(); ++i) {
    pts.push_back({coord(rng), coord(rng), coord(rng)});
    ws.push_back(weight(rng));
  }
  const auto plain = NurbsCurve3::make(kv, pts).value();
  const auto rational = NurbsCurve3::make(kv, pts, ws).value();
  constexpr int kCalls = 1'000'000;
  const double a = ns_per_point(plain, kCalls), b = ns_per_point(rational, kCalls);
  std::printf("cubic, 20 control points, point(u) at random u (median of 5 × %d):\n", kCalls);
  std::printf("  non-rational %.1f ns\n  rational     %.1f ns\n  gate ≤ 50 ns: %s\n", a, b,
              a <= 50 && b <= 50 ? "met" : "NOT met");
  return a <= 50 && b <= 50 ? 0 : 1;
}
