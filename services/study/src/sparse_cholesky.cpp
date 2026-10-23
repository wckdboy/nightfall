// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "sparse_cholesky.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#if defined(__APPLE__)
#include <Accelerate/Accelerate.h>
#endif

namespace nf::study {
bool cholesky_solve(int n, std::span<const int> rows, std::span<const int> cols,
                    std::span<const double> values, std::span<const double> b,
                    std::span<double> x) {
#if defined(__APPLE__)
  if (n <= 0 || rows.size() != static_cast<std::size_t>(n) + 1 || b.size() != x.size() ||
      x.size() != static_cast<std::size_t>(n))
    return false;
  // The lower triangle in compressed columns: A is symmetric, so row r of its CSR is column r,
  // and that column's entries on or below the diagonal are the row's columns c >= r.
  std::vector<long> starts(static_cast<std::size_t>(n) + 1);
  std::vector<int> index;
  std::vector<double> data;
  index.reserve(values.size() / 2 + static_cast<std::size_t>(n));
  data.reserve(index.capacity());
  for (int r = 0; r < n; ++r) {
    starts[static_cast<std::size_t>(r)] = static_cast<long>(index.size());
    for (int k = rows[static_cast<std::size_t>(r)]; k < rows[static_cast<std::size_t>(r) + 1]; ++k)
      if (cols[static_cast<std::size_t>(k)] >= r) {
        index.push_back(cols[static_cast<std::size_t>(k)]);
        data.push_back(values[static_cast<std::size_t>(k)]);
      }
  }
  starts[static_cast<std::size_t>(n)] = static_cast<long>(index.size());
  SparseMatrixStructure structure{};
  structure.rowCount = n;
  structure.columnCount = n;
  structure.columnStarts = starts.data();
  structure.rowIndices = index.data();
  structure.attributes.kind = SparseSymmetric;
  structure.attributes.triangle = SparseLowerTriangle;
  structure.blockSize = 1;
  const SparseMatrix_Double matrix{structure, data.data()};
  auto factor = SparseFactor(SparseFactorizationCholesky, matrix);
  if (factor.status != SparseStatusOK) {
    SparseCleanup(factor);
    return false;
  }
  std::vector<double> rhs(b.begin(), b.end());
  std::vector<double> solution(rhs.size()), correction(rhs.size());
  SparseSolve(factor, DenseVector_Double{n, rhs.data()}, DenseVector_Double{n, solution.data()});
  // Iterative refinement: the factor's rounding leaves a residual near 1e-9 of the load on a
  // stiffness matrix with a wide spread of scales; a few solves for the residual's correction,
  // with the same factor, bring it to the solver's 1e-12 and below.
  double norm = 0;
  for (const double v : b) norm += v * v;
  norm = std::sqrt(norm);
  for (int step = 0; step < 3 && norm > 0; ++step) {
    double residual = 0;
    for (int r = 0; r < n; ++r) {
      double ax = 0;
      for (int k = rows[static_cast<std::size_t>(r)]; k < rows[static_cast<std::size_t>(r) + 1];
           ++k)
        ax += values[static_cast<std::size_t>(k)] *
              solution[static_cast<std::size_t>(cols[static_cast<std::size_t>(k)])];
      rhs[static_cast<std::size_t>(r)] = b[static_cast<std::size_t>(r)] - ax;
      residual += rhs[static_cast<std::size_t>(r)] * rhs[static_cast<std::size_t>(r)];
    }
    if (std::sqrt(residual) <= 1e-14 * norm) break;
    SparseSolve(factor, DenseVector_Double{n, rhs.data()},
                DenseVector_Double{n, correction.data()});
    for (std::size_t i = 0; i < solution.size(); ++i) solution[i] += correction[i];
  }
  SparseCleanup(factor);
  std::ranges::copy(solution, x.begin());
  return true;
#else
  (void)n, (void)rows, (void)cols, (void)values, (void)b, (void)x;
  return false;
#endif
}
}  // namespace nf::study
