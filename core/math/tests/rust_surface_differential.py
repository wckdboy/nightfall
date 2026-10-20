#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
"""Seeded surface comparison with independent 80-digit recurrence adjudication."""
from bisect import bisect_right
from decimal import Decimal, localcontext
from functools import cache
import math
from pathlib import Path
import random
import subprocess
import sys

ORDERS = [(k, l) for k in range(4) for l in range(4-k)]


def cases():
    rng = random.Random(20261021)
    for p in range(1, 10):
        for q in range(1, 10):
            for rational in (False, True):
                for repeated in (False, True):
                    def knots(degree):
                        interior = [0.3]+[0.5]*degree+[0.85] if repeated else []
                        return [0.]*(degree+1)+interior+[1.]*(degree+1)
                    ku, kv = knots(p), knots(q)
                    n, m = len(ku)-p-1, len(kv)-q-1
                    points = [[rng.randint(-128, 128)/16 for _ in range(3)] for _ in range(n*m)]
                    weights = [rng.randint(1, 8)/4 for _ in points] if rational else []
                    samples = [(-2., 2.), (0., 0.), (1., 1.), (0.3, 0.85),
                               (0.5, 0.5), (0.85, 0.3), (2., -2.)]
                    samples += [(rng.random(), rng.random()) for _ in range(4)]
                    for u, v in samples:
                        fields = [p, q, len(ku), len(kv), u, v, int(rational), *ku, *kv,
                                  *(x for point in points for x in point), *weights]
                        yield ' '.join(map(str, fields)), p, q, ku, kv, points, weights, u, v


def close(actual, expected, label):
    largest = 0.
    for row in range(11):
        a, b = actual[3*row:3*row+3], expected[3*row:3*row+3]
        norm = max(1., sum(map(abs, b)))
        relative = 1e-12 if row < 2 else 1e-10
        for x, y in zip(a, b, strict=True):
            tolerance = max(relative, relative*abs(y), 16*math.ulp(1.)*norm)
            assert math.isfinite(x) and math.isfinite(y) and abs(x-y) <= tolerance, (label, row, x, y, tolerance)
            largest = max(largest, abs(x-y)/norm)
    return largest


def reference(case):
    _, p, q, raw_u, raw_v, raw_points, raw_weights, u, v = case
    with localcontext() as context:
        context.prec = 80
        dec = Decimal.from_float
        ku, kv = list(map(dec, raw_u)), list(map(dec, raw_v))
        points = [list(map(dec, point)) for point in raw_points]
        weights = list(map(dec, raw_weights)) if raw_weights else [Decimal(1)]*len(points)
        nu, nv = len(ku)-p-1, len(kv)-q-1

        def evaluator(knots, degree, raw_parameter, count):
            parameter = dec(min(max(raw_parameter, float(knots[degree])), float(knots[-degree-1])))
            span = min(max(bisect_right(knots, parameter)-1, degree), count-1)

            @cache
            def basis(i, d, k):
                if k > d:
                    return Decimal(0)
                if d == 0:
                    return Decimal(int(i == span))
                a, b = knots[i+d]-knots[i], knots[i+d+1]-knots[i+1]
                if k:
                    return (d*basis(i, d-1, k-1)/a if a else Decimal(0)) - (d*basis(i+1, d-1, k-1)/b if b else Decimal(0))
                return ((parameter-knots[i])*basis(i, d-1, 0)/a if a else Decimal(0)) + ((knots[i+d+1]-parameter)*basis(i+1, d-1, 0)/b if b else Decimal(0))
            return basis

        bu, bv = evaluator(ku, p, u, nu), evaluator(kv, q, v, nv)
        ad, wd, result = {}, {}, {}
        for k, l in ORDERS:
            c = [bu(i, p, k)*bv(j, q, l)*weights[i*nv+j] for i in range(nu) for j in range(nv)]
            wd[k, l] = sum(c)
            ad[k, l] = [sum(a*point[axis] for a, point in zip(c, points, strict=True)) for axis in range(3)]
            result[k, l] = []
            for axis in range(3):
                correction = sum(Decimal(math.comb(k, i)*math.comb(l, j))*wd[i, j]*result[k-i, l-j][axis]
                                 for i in range(k+1) for j in range(l+1) if i+j)
                result[k, l].append((ad[k, l][axis]-correction)/wd[0, 0])
        return [float(x) for x in result[0, 0]] + [float(x) for order in ORDERS for x in result[order]]


def main():
    oracle, root = sys.argv[1], Path(sys.argv[2])
    inputs = list(cases())
    payload = '\n'.join(case[0] for case in inputs)+'\n'
    commands = [[oracle], ['cargo', 'run', '--quiet', '--release', '--locked', '--offline',
                          '--example', 'nf-rust-surface-probe']]
    outputs = [subprocess.run(c, cwd=root, input=payload, text=True, capture_output=True, check=True).stdout.splitlines() for c in commands]
    assert len(outputs[0]) == len(outputs[1]) == len(inputs)
    largest, independent, adjudications = 0., 0, 0
    for index, (left, right, case) in enumerate(zip(*outputs, inputs, strict=True)):
        _, p, q, ku, kv, points, weights, _, _ = case
        count = len(points)
        native, rust = list(map(float, left.split())), list(map(float, right.split()))
        assert len(native) == len(rust) == 1+count+33
        assert native[:count+1] == rust[:count+1], (index, 'weights/rational flag')
        native, rust = native[count+1:], rust[count+1:]
        if not weights:
            for row, (k, l) in enumerate(ORDERS, start=1):
                if k > p or l > q:
                    assert rust[3*row:3*row+3] == [0., 0., 0.], index
        needs_reference = False
        try:
            largest = max(largest, close(rust, native, index))
        except AssertionError:
            assert all(map(math.isfinite, native))
            needs_reference = True
            adjudications += 1
        if index % 64 == 0 or needs_reference:
            close(rust, reference(case), (index, '80-digit independent reference'))
            independent += 1
    print(f'Rust/C++ surfaces: {len(inputs)} seeded cases, u/v degrees 1-9, mixed total order <=3; '
          f'max directly conforming row-scaled difference {largest:.3g}; '
          f'{independent} independent 80-digit checks, {adjudications} legacy disagreements adjudicated')


if __name__ == '__main__':
    main()
