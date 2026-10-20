#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
"""Seeded 2D/3D rational-curve conformance; no production interchange format."""
from bisect import bisect_right
from decimal import Decimal, localcontext
from functools import cache
import math
from pathlib import Path
import random
import subprocess
import sys


def cases():
    rng = random.Random(20261020)
    for dimension in (2, 3):
        for degree in range(1, 10):
            for weighted in (0, 1):
                for repeated in (False, True):
                    knots = [0.0] * (degree + 1)
                    if repeated:
                        knots += [0.3] + [0.5] * degree + [0.85]
                    knots += [1.0] * (degree + 1)
                    count = len(knots) - degree - 1
                    points = [[rng.randint(-128, 128) / 16 for _ in range(dimension)]
                              for _ in range(count)]
                    weights = [rng.randint(1, 8) / 4 for _ in range(count)] if weighted else []
                    samples = [-2.0, 0.0, 0.3, 0.5, 0.85, 1.0, 3.0]
                    samples += [rng.random() for _ in range(40)]
                    for u in samples:
                        fields = [dimension, degree, len(knots), u, weighted, *knots,
                                  *(v for point in points for v in point), *weights]
                        yield " ".join(map(str, fields)), dimension, degree, knots, points, weights, u


def close_rows(actual, expected, dimension, label):
    largest = 0.0
    for row in range(len(expected) // dimension):
        a = actual[row*dimension:(row+1)*dimension]
        e = expected[row*dimension:(row+1)*dimension]
        norm = max(1.0, sum(abs(v) for v in e))
        relative = 1e-12 if row < 2 else 1e-10  # point, C, C', C'', …
        for x, y in zip(a, e, strict=True):
            assert math.isfinite(x) and math.isfinite(y), label
            tolerance = max(relative, relative * abs(y), 16 * math.ulp(1.0) * norm)
            assert abs(x-y) <= tolerance, (label, row, x, y, tolerance)
            largest = max(largest, abs(x-y) / norm)
    return largest


def decimal_reference(case):
    _, dimension, degree, raw_knots, raw_points, raw_weights, parameter = case
    with localcontext() as context:
        context.prec = 80
        dec = Decimal.from_float
        knots = list(map(dec, raw_knots))
        points = [list(map(dec, p)) for p in raw_points]
        weights = list(map(dec, raw_weights)) if raw_weights else [Decimal(1)] * len(points)
        u = dec(min(max(parameter, raw_knots[degree]), raw_knots[-degree-1]))
        span = min(max(bisect_right(knots, u)-1, degree), len(points)-1)

        @cache
        def basis(i, p, k):
            if k > p:
                return Decimal(0)
            if p == 0:
                return Decimal(int(i == span))
            a, b = knots[i+p]-knots[i], knots[i+p+1]-knots[i+1]
            if k:
                left = p * basis(i, p-1, k-1) / a if a else Decimal(0)
                right = p * basis(i+1, p-1, k-1) / b if b else Decimal(0)
                return left-right
            left = (u-knots[i]) * basis(i, p-1, 0) / a if a else Decimal(0)
            right = (knots[i+p+1]-u) * basis(i+1, p-1, 0) / b if b else Decimal(0)
            return left+right

        aders, wders, result = [], [], []
        for k in range(13):
            coefficients = [basis(i, degree, k)*weights[i] for i in range(len(points))]
            wders.append(sum(coefficients))
            aders.append([sum(c*p[axis] for c, p in zip(coefficients, points, strict=True))
                          for axis in range(dimension)])
            row = []
            for axis in range(dimension):
                correction = sum(Decimal(math.comb(k, j))*wders[j]*result[k-j][axis]
                                 for j in range(1, k+1))
                row.append((aders[k][axis]-correction)/wders[0])
            result.append(row)
        return [float(v) for v in result[0]] + [float(v) for row in result for v in row]


def main():
    oracle, root = sys.argv[1], Path(sys.argv[2])
    inputs = list(cases())
    payload = "\n".join(case[0] for case in inputs) + "\n"
    commands = [[oracle], ["cargo", "run", "--quiet", "--release", "--locked",
                          "--offline", "--example", "nf-rust-curve-probe"]]
    outputs = [subprocess.run(command, cwd=root, input=payload, text=True,
                              capture_output=True, check=True).stdout.splitlines()
               for command in commands]
    assert len(outputs[0]) == len(outputs[1]) == len(inputs)
    largest, references, native_polynomial_residual, adjudications = 0.0, 0, 0.0, 0
    for index, (left, right, case) in enumerate(zip(*outputs, inputs, strict=True)):
        _, dimension, degree, knots, points, weights, _ = case
        count = len(knots)-degree-1
        native, rust = list(map(float, left.split())), list(map(float, right.split()))
        assert len(native) == len(rust) == 1+count+14*dimension
        assert native[:count+1] == rust[:count+1], (index, "rational flag/normalized weights")
        native, rust = native[count+1:], rust[count+1:]
        # Polynomial derivatives above p are exactly zero. C++'s quotient recurrence
        # has known cancellation residuals (#193); Rust uses the polynomial path.
        rows = 14 if weights and any(w != weights[0] for w in weights) else degree+2
        needs_reference = False
        try:
            largest = max(largest, close_rows(rust[:rows*dimension], native[:rows*dimension], dimension, index))
        except AssertionError:
            # An inexact legacy result is not the mathematical reference. Every
            # disagreement must pass the SAME strict Rust tolerances against the
            # independent 80-digit recurrence; no tolerance is raised to match C++.
            assert all(math.isfinite(v) for v in native)
            needs_reference = True
            adjudications += 1
        if rows < 14:
            assert all(v == 0.0 for v in rust[rows*dimension:]), index
            native_polynomial_residual = max(native_polynomial_residual,
                                            max(map(abs, native[rows*dimension:]), default=0.0))
        if index % 64 == 0 or needs_reference:
            reference = decimal_reference(case)
            close_rows(rust, reference, dimension, (index, "80-digit Rust"))
            references += 1
    print(f"Rust/C++ curves: {len(inputs)} cases, 2D/3D degrees 1–9, derivatives through 12; "
          f"max directly conforming row-scaled difference {largest:.3g}; "
          f"{references} independent 80-digit checks, {adjudications} legacy disagreements adjudicated; "
          f"native above-degree polynomial residual {native_polynomial_residual:.3g}, Rust exactly zero")


if __name__ == "__main__":
    main()
