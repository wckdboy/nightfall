// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
// Test-only differential protocol; never linked into the kernel or client.
#include <iomanip>
#include <iostream>
#include <vector>

#include "nf/math/basis.hpp"

int main() {
  int degree = 0, order = 0;
  std::size_t count = 0;
  double u = 0.0;
  std::cout << std::setprecision(17) << std::scientific;
  while (std::cin >> degree >> count >> u >> order) {
    std::vector<double> values(count);
    for (double& value : values) {
      if (!(std::cin >> value)) return 1;
    }
    auto knots = nf::math::KnotVector::make(std::move(values), degree);
    if (!knots) return 2;
    const int span = knots->find_span(u);
    nf::math::BasisValues basis{};
    nf::math::BasisDerivatives derivatives{};
    nf::math::basis_functions(*knots, span, u, basis);
    nf::math::basis_derivatives(*knots, span, u, order, derivatives);
    std::cout << span;
    for (double value : basis) std::cout << ' ' << value;
    for (double value : derivatives.values) std::cout << ' ' << value;
    std::cout << '\n';
  }
  return std::cin.eof() ? 0 : 3;
}
