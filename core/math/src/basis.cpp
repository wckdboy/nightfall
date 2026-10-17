// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "nf/math/basis.hpp"

#include <algorithm>
#include <utility>

namespace nf::math {

namespace {

constexpr int kSide = KnotVector::kMaxDegree + 1;
using Row = std::array<double, kSide>;

double knot(const KnotVector& k, int i) noexcept { return k[static_cast<std::size_t>(i)]; }

}  // namespace

// The NURBS Book, A2.2.
void basis_functions(const KnotVector& kv, int span, double u, BasisValues& out) noexcept {
  const int p = kv.degree();
  u = std::clamp(u, kv.domain_start(), kv.domain_end());
  Row left{}, right{};
  out[0] = 1.0;
  for (int j = 1; j <= p; ++j) {
    left[static_cast<std::size_t>(j)] = u - knot(kv, span + 1 - j);
    right[static_cast<std::size_t>(j)] = knot(kv, span + j) - u;
    double saved = 0.0;
    for (int r = 0; r < j; ++r) {
      const double temp =
          out[static_cast<std::size_t>(r)] /
          (right[static_cast<std::size_t>(r + 1)] + left[static_cast<std::size_t>(j - r)]);
      out[static_cast<std::size_t>(r)] = saved + right[static_cast<std::size_t>(r + 1)] * temp;
      saved = left[static_cast<std::size_t>(j - r)] * temp;
    }
    out[static_cast<std::size_t>(j)] = saved;
  }
}

// The NURBS Book, A2.3.
void basis_derivatives(const KnotVector& kv, int span, double u, int n,
                       BasisDerivatives& out) noexcept {
  const int p = kv.degree();
  u = std::clamp(u, kv.domain_start(), kv.domain_end());
  const int orders = std::min(n, p);  // higher derivatives of a degree-p basis are zero
  std::array<Row, kSide> ndu{};
  Row left{}, right{};
  const auto at = [](auto& m, int r, int c) -> auto& {
    return m[static_cast<std::size_t>(r)][static_cast<std::size_t>(c)];
  };
  const auto idx = [](int i) { return static_cast<std::size_t>(i); };

  at(ndu, 0, 0) = 1.0;
  for (int j = 1; j <= p; ++j) {
    left[idx(j)] = u - knot(kv, span + 1 - j);
    right[idx(j)] = knot(kv, span + j) - u;
    double saved = 0.0;
    for (int r = 0; r < j; ++r) {
      at(ndu, j, r) = right[idx(r + 1)] + left[idx(j - r)];  // lower triangle: knot differences
      const double temp = at(ndu, r, j - 1) / at(ndu, j, r);
      at(ndu, r, j) = saved + right[idx(r + 1)] * temp;  // upper triangle: basis functions
      saved = left[idx(j - r)] * temp;
    }
    at(ndu, j, j) = saved;
  }

  out.values.fill(0.0);
  for (int j = 0; j <= p; ++j) out.at(0, j) = at(ndu, j, p);

  std::array<Row, 2> a{};
  for (int r = 0; r <= p; ++r) {
    int s1 = 0, s2 = 1;
    at(a, 0, 0) = 1.0;
    for (int k = 1; k <= orders; ++k) {
      double d = 0.0;
      const int rk = r - k, pk = p - k;
      if (r >= k) {
        at(a, s2, 0) = at(a, s1, 0) / at(ndu, pk + 1, rk);
        d = at(a, s2, 0) * at(ndu, rk, pk);
      }
      const int j1 = rk >= -1 ? 1 : -rk;
      const int j2 = (r - 1 <= pk) ? k - 1 : p - r;
      for (int j = j1; j <= j2; ++j) {
        at(a, s2, j) = (at(a, s1, j) - at(a, s1, j - 1)) / at(ndu, pk + 1, rk + j);
        d += at(a, s2, j) * at(ndu, rk + j, pk);
      }
      if (r <= pk) {
        at(a, s2, k) = -at(a, s1, k - 1) / at(ndu, pk + 1, r);
        d += at(a, s2, k) * at(ndu, r, pk);
      }
      out.at(k, r) = d;
      std::swap(s1, s2);
    }
  }
  double factor = p;  // multiply by p! / (p - k)!
  for (int k = 1; k <= orders; ++k) {
    for (int j = 0; j <= p; ++j) out.at(k, j) *= factor;
    factor *= p - k;
  }
}

}  // namespace nf::math
