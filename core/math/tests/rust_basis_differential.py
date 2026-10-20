#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
"""Seeded independent-process comparison; every numerical tolerance is explicit."""
import math
from decimal import Decimal, localcontext
from functools import cache
from pathlib import Path
import random
import subprocess
import sys


def cases():
    rng = random.Random(20261020)
    for degree in range(1, 10):
        for scale, offset in [(1.0, 0.0), (1e-3, -7.0), (1234.0, 6.0)]:
            interior = [offset + scale * x for x in (0.07, 0.2, 0.5, 0.81)]
            knots = [offset] * (degree + 1)
            for value in interior:
                knots += [value] * rng.randint(1, degree)
            end = offset + scale
            knots += [end] * (degree + 1)
            samples = [offset - scale, offset, end, end + scale]
            for value in interior:
                samples += [math.nextafter(value, -math.inf), value,
                            math.nextafter(value, math.inf)]
            samples += [offset + scale * rng.random() for _ in range(112)]
            for i, u in enumerate(samples):
                order = 9 if i % 2 == 0 else rng.randint(0, 9)
                yield " ".join(map(str, [degree, len(knots), u, order, *knots]))


def main():
    oracle, root = sys.argv[1], Path(sys.argv[2])
    inputs = list(cases())
    payload = "\n".join(inputs) + "\n"
    commands = [[oracle], ["cargo", "run", "--quiet", "--release", "--locked",
                          "--offline", "--example", "nf-rust-basis-probe"]]
    outputs = [subprocess.run(command, cwd=root, input=payload, text=True,
                              capture_output=True, check=True).stdout.splitlines()
               for command in commands]
    assert len(outputs[0]) == len(outputs[1]) == len(inputs)
    largest_relative, largest_row_scaled, references = 0.0, 0.0, 0
    for case, (a, b) in enumerate(zip(*outputs, strict=True)):
        native, rust = list(map(float, a.split())), list(map(float, b.split()))
        assert len(native) == len(rust) == 111, (case, inputs[case])
        assert native[0] == rust[0], (case, "span", inputs[case])
        for index, (expected, actual) in enumerate(zip(native[1:], rust[1:], strict=True)):
            assert math.isfinite(expected) and math.isfinite(actual), (case, index)
            relative = abs(expected - actual) / max(1.0, abs(expected), abs(actual))
            largest_relative = max(largest_relative, relative)
            # High derivatives can nearly cancel inside a row. Include a four-ulp
            # row-norm floor, rather than demanding relative accuracy at a near-zero
            # component; the independent Decimal recurrence below checks this floor.
            begin = 1 if index < 10 else 11 + (index - 10) // 10 * 10
            norm = max(1.0, sum(abs(v) for v in native[begin:begin + 10]))
            largest_row_scaled = max(largest_row_scaled, abs(actual - expected) / norm)
            tolerance = max(1e-12, 1e-12 * abs(expected), 4 * math.ulp(1.0) * norm)
            assert abs(actual - expected) <= tolerance, (
                case, index, expected, actual, inputs[case])
        if case % 32 == 0 or any(not math.isclose(a, b, rel_tol=1e-12, abs_tol=1e-12)
                                 for a, b in zip(native[1:], rust[1:], strict=True)):
            check_decimal(inputs[case], native)
            check_decimal(inputs[case], rust)
            references += 1
    print(f"Rust/C++: {len(inputs)} cases, 110 values/case, degrees 1–9; "
          f"max component-scaled difference {largest_relative:.3g}, "
          f"row-scaled {largest_row_scaled:.3g}; {references} independent 80-digit checks")


def check_decimal(case, output):
    """Cox-de Boor / recursive derivative oracle, independent of triangular A2.3."""
    fields = case.split()
    degree, order, span = int(fields[0]), int(fields[3]), int(output[0])
    with localcontext() as context:
        context.prec = 80
        knots = [Decimal.from_float(float(v)) for v in fields[4:]]
        u = min(max(Decimal.from_float(float(fields[2])), knots[degree]), knots[-degree-1])

        @cache
        def value(i, p, k):
            if k > p:
                return Decimal(0)
            if p == 0:
                return Decimal(int(i == span))
            a, b = knots[i+p] - knots[i], knots[i+p+1] - knots[i+1]
            if k:
                left = p * value(i, p-1, k-1) / a if a else Decimal(0)
                right = p * value(i+1, p-1, k-1) / b if b else Decimal(0)
                return left - right
            left = (u-knots[i]) * value(i, p-1, 0) / a if a else Decimal(0)
            right = (knots[i+p+1]-u) * value(i+1, p-1, 0) / b if b else Decimal(0)
            return left + right

        for k in range(order+1):
            expected = [float(value(span-degree+j, degree, k)) for j in range(degree+1)]
            norm = max(1.0, sum(abs(v) for v in expected))
            for j, exact in enumerate(expected):
                # Eight ulps of the row norm bounds accumulated double rounding here.
                tolerance = max(1e-12, 1e-12 * abs(exact), 8 * math.ulp(1.0) * norm)
                assert abs(output[11+k*10+j] - exact) <= tolerance, (case, k, j, exact)


if __name__ == "__main__":
    main()
