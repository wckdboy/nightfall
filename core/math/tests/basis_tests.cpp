// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_adapters.hpp>
#include <catch2/generators/catch_generators_random.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <stdexcept>
#include <vector>

#include "nf/math/basis.hpp"
#include "nf/math/knot_vector.hpp"

using namespace nf::math;
using Catch::Matchers::WithinAbs;

namespace {

// The NURBS Book, Example 2.3: U = {0,0,0,1,2,3,4,4,5,5,5}, p = 2.
KnotVector book_example() { return KnotVector::make({0, 0, 0, 1, 2, 3, 4, 4, 5, 5, 5}, 2).value(); }

}  // namespace

TEST_CASE("knot vectors are validated", "[knots]") {
  CHECK(KnotVector::make({0, 0, 1, 1}, 1).has_value());
  CHECK_FALSE(KnotVector::make({0, 0, 1, 1}, 0).has_value());        // degree < 1
  CHECK_FALSE(KnotVector::make({0, 0, 1}, 1).has_value());           // too short
  CHECK_FALSE(KnotVector::make({0, 0, 2, 1, 2, 2}, 1).has_value());  // decreasing
  CHECK_FALSE(KnotVector::make({0, 1, 1, 2, 2}, 1).has_value());     // not clamped at start
  CHECK_FALSE(KnotVector::make({0, 0, 0, 0}, 1).has_value());        // empty domain
  CHECK_FALSE(KnotVector::make({0, 0, NAN, 1, 1}, 1).has_value());   // not finite
  CHECK_FALSE(KnotVector::make({0, 0, 1, 1, 1, 1}, 1).has_value());  // end multiplicity > p+1
  CHECK_FALSE(
      KnotVector::make({0, 0, 0.5, 0.5, 1, 1}, 1).has_value());  // interior multiplicity > p
  const auto error = KnotVector::make({0, 0, 1}, 1);
  CHECK_FALSE(error.error().empty());
}

TEST_CASE("knot vector domain and control point count", "[knots]") {
  const KnotVector kv = book_example();
  CHECK(kv.degree() == 2);
  CHECK(kv.size() == 11);
  CHECK(kv.control_point_count() == 8);
  CHECK(kv.domain_start() == 0.0);
  CHECK(kv.domain_end() == 5.0);
}

TEST_CASE("uniform clamped knot vectors", "[knots]") {
  const KnotVector kv = KnotVector::uniform_clamped(3, 7);
  CHECK(kv.control_point_count() == 7);
  const std::vector<double> expected{0, 0, 0, 0, 0.25, 0.5, 0.75, 1, 1, 1, 1};
  CHECK(std::vector<double>(kv.knots().begin(), kv.knots().end()) == expected);
}

TEST_CASE("uniform_clamped rejects invalid degrees and counts", "[knots]") {
  CHECK_THROWS_AS(KnotVector::uniform_clamped(-2, 0), std::invalid_argument);
  CHECK_THROWS_AS(KnotVector::uniform_clamped(0, 3), std::invalid_argument);
  CHECK_THROWS_AS(KnotVector::uniform_clamped(KnotVector::kMaxDegree + 1, 20),
                  std::invalid_argument);
  CHECK_THROWS_AS(KnotVector::uniform_clamped(3, 3), std::invalid_argument);
}

TEST_CASE("basis functions clamp u to the domain", "[basis]") {
  const KnotVector kv = KnotVector::make({0, 0, 1, 1}, 1).value();
  for (const double u : {-1.0, 2.0}) {
    BasisValues n;
    basis_functions(kv, kv.find_span(u), u, n);
    BasisDerivatives d;
    basis_derivatives(kv, kv.find_span(u), u, 1, d);
    const double inside = u < 0 ? 0.0 : 1.0;
    BasisValues e;
    basis_functions(kv, kv.find_span(inside), inside, e);
    CHECK(n[0] == e[0]);
    CHECK(n[1] == e[1]);
    CHECK(d.at(0, 0) == e[0]);
    CHECK(d.at(0, 1) == e[1]);
  }
}

TEST_CASE("span search (A2.1) finds the non-empty interval holding u", "[knots]") {
  const KnotVector kv = book_example();
  CHECK(kv.find_span(0.0) == 2);
  CHECK(kv.find_span(0.5) == 2);
  CHECK(kv.find_span(1.0) == 3);
  CHECK(kv.find_span(2.5) == 4);
  CHECK(kv.find_span(4.0) == 7);  // skips the empty interval [4, 4)
  CHECK(kv.find_span(5.0) == 7);  // the end of the domain belongs to the last span
}

TEST_CASE("basis functions match The NURBS Book, Example 2.3", "[basis]") {
  const KnotVector kv = book_example();
  const double u = 2.5;
  const int span = kv.find_span(u);
  REQUIRE(span == 4);
  BasisValues n;
  basis_functions(kv, span, u, n);
  CHECK_THAT(n[0], WithinAbs(1.0 / 8.0, 1e-15));  // N_{2,2}
  CHECK_THAT(n[1], WithinAbs(6.0 / 8.0, 1e-15));  // N_{3,2}
  CHECK_THAT(n[2], WithinAbs(1.0 / 8.0, 1e-15));  // N_{4,2}
}

TEST_CASE("basis functions are a non-negative partition of unity", "[basis][property]") {
  const int degree = GENERATE(1, 2, 3, 5);
  const KnotVector kv = KnotVector::uniform_clamped(degree, degree + 6);
  const double u = GENERATE(take(50, random(0.0, 1.0)));
  BasisValues n;
  basis_functions(kv, kv.find_span(u), u, n);
  double sum = 0.0;
  for (std::size_t i = 0; i <= static_cast<std::size_t>(degree); ++i) {
    CHECK(n[i] >= -1e-15);
    sum += n[i];
  }
  CHECK_THAT(sum, WithinAbs(1.0, 1e-14));
}

TEST_CASE("basis derivatives (A2.3) match finite differences", "[basis][property]") {
  const int degree = GENERATE(2, 3, 4);
  const KnotVector kv = KnotVector::make({0, 0, 0, 0, 0, 0.3, 0.45, 0.7, 1, 1, 1, 1, 1}, 4).value();
  const KnotVector use = degree == 4 ? kv : KnotVector::uniform_clamped(degree, degree + 5);
  const double u = GENERATE(take(30, random(0.02, 0.98)));
  const int span = use.find_span(u);
  BasisDerivatives d;
  basis_derivatives(use, span, u, 2, d);

  // Central differences of the 0th and 1st derivative, staying inside one span.
  constexpr double h = 1e-6;
  const auto values_at = [&](double x) {
    BasisDerivatives e;
    basis_derivatives(use, span, x, 2, e);
    return e;
  };
  const BasisDerivatives plus = values_at(u + h), minus = values_at(u - h);
  if (use.find_span(u + h) != span || use.find_span(u - h) != span) return;  // straddles a knot
  for (int i = 0; i <= use.degree(); ++i) {
    const double d1 = (plus.at(0, i) - minus.at(0, i)) / (2 * h);
    const double d2 = (plus.at(1, i) - minus.at(1, i)) / (2 * h);
    CHECK_THAT(d.at(1, i), WithinAbs(d1, 1e-6 * (1 + std::abs(d1))));
    CHECK_THAT(d.at(2, i), WithinAbs(d2, 1e-4 * (1 + std::abs(d2))));
  }
  // Derivatives of a partition of unity sum to zero.
  double s1 = 0, s2 = 0;
  for (int i = 0; i <= use.degree(); ++i) {
    s1 += d.at(1, i);
    s2 += d.at(2, i);
  }
  CHECK_THAT(s1, WithinAbs(0.0, 1e-9));
  CHECK_THAT(s2, WithinAbs(0.0, 1e-7));
}
