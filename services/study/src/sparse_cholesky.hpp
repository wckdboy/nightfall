// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// A direct solve of a symmetric positive definite system with the operating system's sparse
// Cholesky (the Accelerate framework on macOS). Standard types only, so its headers stay apart
// from MFEM's.
#pragma once

#include <span>

namespace nf::study {
/// Solves A x = b for the symmetric positive definite n x n matrix A given as CSR: `rows` (n + 1
/// starts), `cols` (sorted within each row) and `values`. Returns false, leaving `x` as it was,
/// where the system has no sparse direct solver or A does not factor (not positive definite).
bool cholesky_solve(int n, std::span<const int> rows, std::span<const int> cols,
                    std::span<const double> values, std::span<const double> b, std::span<double> x);
}  // namespace nf::study
