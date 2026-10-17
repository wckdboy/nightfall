// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "nf/math/knot_vector.hpp"

#include <cmath>
#include <stdexcept>

namespace nf::math {

namespace {

std::size_t multiplicity_at(const std::vector<double>& k, std::size_t start) {
  std::size_t m = 1;
  while (start + m < k.size() && k[start + m] == k[start]) ++m;
  return m;
}

}  // namespace

std::expected<KnotVector, std::string> KnotVector::make(std::vector<double> knots, int degree) {
  if (degree < 1 || degree > kMaxDegree) {
    return std::unexpected("degree must be in [1, " + std::to_string(kMaxDegree) + "]");
  }
  const auto p = static_cast<std::size_t>(degree);
  if (knots.size() < 2 * (p + 1)) {
    return std::unexpected("need at least 2(p + 1) knots");
  }
  for (std::size_t i = 0; i < knots.size(); ++i) {
    if (!std::isfinite(knots[i])) return std::unexpected("knots must be finite");
    if (i > 0 && knots[i] < knots[i - 1]) return std::unexpected("knots must be non-decreasing");
  }
  if (multiplicity_at(knots, 0) != p + 1) {
    return std::unexpected("first knot must have multiplicity p + 1 (clamped)");
  }
  if (multiplicity_at(knots, knots.size() - p - 1) != p + 1 ||
      knots[knots.size() - p - 2] == knots.back()) {
    return std::unexpected("last knot must have multiplicity p + 1 (clamped)");
  }
  if (!(knots[p] < knots[knots.size() - p - 1])) return std::unexpected("empty domain");
  for (std::size_t i = p + 1; i < knots.size() - p - 1;) {
    const std::size_t m = multiplicity_at(knots, i);
    if (m > p) return std::unexpected("interior knot multiplicity must be <= p");
    i += m;
  }
  return KnotVector(std::make_shared<const std::vector<double>>(std::move(knots)), degree);
}

KnotVector KnotVector::uniform_clamped(int degree, int control_points) {
  if (control_points < degree + 1) {
    throw std::invalid_argument("uniform_clamped: need at least degree + 1 control points");
  }
  std::vector<double> k;
  const int interior = control_points - degree - 1;
  k.insert(k.end(), static_cast<std::size_t>(degree + 1), 0.0);
  for (int i = 1; i <= interior; ++i) k.push_back(static_cast<double>(i) / (interior + 1));
  k.insert(k.end(), static_cast<std::size_t>(degree + 1), 1.0);
  auto made = make(std::move(k), degree);
  if (!made) throw std::invalid_argument("uniform_clamped: " + made.error());
  return *made;
}

int KnotVector::find_span(double u) const noexcept {
  const std::vector<double>& k = *knots_;
  const int n = control_point_count() - 1;
  if (u >= k[static_cast<std::size_t>(n + 1)]) return n;  // end of domain -> last span
  if (u <= k[static_cast<std::size_t>(degree_)]) return degree_;
  int low = degree_, high = n + 1;
  int mid = (low + high) / 2;
  while (u < k[static_cast<std::size_t>(mid)] || u >= k[static_cast<std::size_t>(mid + 1)]) {
    if (u < k[static_cast<std::size_t>(mid)]) {
      high = mid;
    } else {
      low = mid;
    }
    mid = (low + high) / 2;
  }
  return mid;
}

}  // namespace nf::math
