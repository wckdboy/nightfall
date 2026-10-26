#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
"""Independent Rust integration of native loft control nets; private QA only."""
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys

LEVELS = [1, 2, 4, 8, 16]
PREFIX = 2*len(LEVELS)


def payload(patch):
    fields = [patch['degree_u'], patch['degree_v'], len(patch['knots_u']),
              len(patch['knots_v']), patch['orientation'], len(patch['samples']),
              *patch['knots_u'], *patch['knots_v'],
              *(x for point in patch['points'] for x in point), *patch['weights'],
              *(x for sample in patch['samples'] for x in (sample['u'], sample['v']))]
    return ' '.join(map(str, fields))


def analytic_patch(r0, r1, sign):
    arc = [(1., 0.), (1., 1.), (0., 1.)]
    return {'degree_u': 2, 'degree_v': 1, 'knots_u': [2., 2., 2., 7., 7., 7.],
            'knots_v': [-3., -3., 4., 4.], 'orientation': sign, 'samples': [],
            'points': [[500+r*x, r*y, z] for x, y in arc
                       for r, z in [(r0, 20.), (r1, 25.)]],
            'weights': [1., 1., math.sqrt(0.5), math.sqrt(0.5), 1., 1.]}


def correspondence(case):
    waist = case['waist']
    frames = [(100., 0., 28., 8.), (116., math.pi/12, 36*waist, 7*waist),
              (100., math.pi/6, 22., 5.)]
    largest_angle, section_distance, linear_z, weight_variation = 0., 0., 0., 0.
    boundaries = []
    for patch in case['patches']:
        assert patch['degree_u'] == patch['degree_v'] == 2 and len(patch['points']) == 9
        for i in range(5):
            angles = []
            for section, j in enumerate((0, 2, 4)):
                p = patch['samples'][i*5+j]['point']
                r, angle, a, b = frames[section]
                c, s = math.cos(angle), math.sin(angle)
                dx, dy = p[0]-500-r*c, p[1]-r*s
                x, y = dx*c+dy*s, -dx*s+dy*c
                rho = math.hypot(x/a, y/b)
                section_distance = max(section_distance, max(a, b)*abs(rho-1),
                                       abs(p[2]-[52., 117., 182.][section]))
                angles.append(math.atan2(y/b, x/a))
            largest_angle = max(largest_angle, *(abs(math.remainder(a-angles[0], math.tau)) for a in angles[1:]))
            if i == 0:
                boundaries.append(angles)
            for j in (1, 3):
                linear_z = max(linear_z, abs(patch['samples'][i*5+j]['point'][2]-(52+130*j/4)))
            # Each exported patch is a single biquadratic. Its homogeneous weight is
            # independently evaluated at the three section parameters.
            t = i/4
            bu = [(1-t)**2, 2*t*(1-t), t*t]
            weights = []
            for v in (0., 0.5, 1.):
                bv = [(1-v)**2, 2*v*(1-v), v*v]
                weights.append(sum(bu[k]*bv[l]*patch['weights'][3*k+l] for k in range(3) for l in range(3)))
            weight_variation = max(weight_variation, *(abs(w/weights[0]-1) for w in weights[1:]))
    assert section_distance <= 1e-6, ('section interpolation mm', section_distance)
    assert largest_angle > 1e-6 or linear_z > 1e-6, 'fixed-parameter assumption no longer contradicted'
    return {'max_section_distance_bound_mm': section_distance,
            'max_section_ellipse_angle_difference_rad': largest_angle,
            'max_deviation_from_linear_z_mm': linear_z,
            'max_homogeneous_section_weight_relative_variation': weight_variation,
            'patch_start_section_angles_rad': boundaries}


def main():
    probe, root = sys.argv[1], Path(sys.argv[2])
    folder = Path(sys.argv[3]) if len(sys.argv) == 4 else None
    if folder is not None:
        assert not folder.exists(), 'output directory must be new'
    fixture = root/'services/kernel/tests/fixtures/eclipse_vane.nightfall'
    source = fixture.read_bytes()
    native = subprocess.run([probe, str(fixture)], cwd=root, capture_output=True, text=True, check=True)
    data = json.loads(native.stdout)
    assert hashlib.sha256(source).hexdigest() == data['input_sha256']
    assert len(data['cases']) == 3
    assert data['cases'][0]['patches'] == data['cases'][2]['patches']
    assert data['cases'][0]['state_hash'] == data['cases'][2]['state_hash']
    assert data['cases'][2]['cache_hits'] == 4
    synthetic = [analytic_patch(1., 1., 1.), analytic_patch(2., 3., 1.), analytic_patch(2., 3., -1.)]
    patches = synthetic + [p for c in data['cases'] for p in c['patches']]
    request = '\n'.join(map(payload, patches))+'\n'
    rust = subprocess.run(['cargo', 'run', '-p', 'nightfall-math', '--quiet', '--release',
                           '--example', 'nf-rust-surface-integral-probe', '--locked', '--offline'],
                          cwd=root, input=request, capture_output=True, text=True, check=True)
    results = [list(map(float, line.split())) for line in rust.stdout.splitlines()]
    assert len(results) == len(patches)
    for index, (r0, r1, sign) in enumerate([(1., 1., 1.), (2., 3., 1.), (2., 3., -1.)]):
        expected_volume = sign*5*math.pi*(r0*r0+r0*r1+r1*r1)/12
        expected_area = math.pi*(r0+r1)*math.hypot(5., r1-r0)/4
        assert abs(results[index][PREFIX-2]-expected_volume) < 1e-10
        assert abs(results[index][PREFIX-1]-expected_area) < 1e-10
    reports = []
    index = 3
    for case in data['cases']:
        volume, area = [0.]*len(LEVELS), [334*math.pi]*len(LEVELS)
        point_difference, derivative_difference = 0., 0.
        for patch in case['patches']:
            row = results[index]
            index += 1
            assert len(row) == PREFIX+9*len(patch['samples']) and all(map(math.isfinite, row))
            for level in range(len(LEVELS)):
                volume[level] += row[2*level]
                area[level] += row[2*level+1]
            for i, sample in enumerate(patch['samples']):
                for group, field in enumerate(('point', 'du', 'dv')):
                    for axis, expected in enumerate(sample[field]):
                        value = row[PREFIX+9*i+3*group+axis]
                        delta = abs(value-expected)
                        tolerance = 1e-9 if field == 'point' else 1e-8*max(1., abs(expected))
                        assert delta <= tolerance, (field, delta, tolerance)
                        if field == 'point': point_difference = max(point_difference, delta)
                        else: derivative_difference = max(derivative_difference, delta)
        assert abs(volume[-1]-volume[-2]) <= 1e-6, ('volume quadrature convergence', volume)
        assert abs(area[-1]-area[-2]) <= 1e-6, ('area quadrature convergence', area)
        assert abs(volume[-1]-case['volume_mm3']) <= 0.02/12, ('volume comparison', volume[-1], case['volume_mm3'])
        assert abs(area[-1]-case['area_mm2']) <= 0.05/12, ('area comparison', area[-1], case['area_mm2'])
        assert case['solids'] == 1 and case['faces'] == 10 and case['edges'] == 24
        assert case['caps'] == [{'normal_z': -1, 'z': 52}, {'normal_z': 1, 'z': 182}]
        reports.append({'waist': case['waist'], 'native_volume_mm3': case['volume_mm3'],
            'native_area_mm2': case['area_mm2'], 'rust_volume_mm3': volume[-1], 'rust_area_mm2': area[-1],
            'subdivisions_per_knot_span': LEVELS, 'volume_refinement_mm3': volume,
            'area_refinement_mm2': area, 'max_point_difference_mm': point_difference,
            'max_derivative_component_difference': derivative_difference, 'native_evaluation_ms': case['evaluation_ms'],
            'correspondence': correspondence(case)})
    assert fixture.read_bytes() == source
    receipt = {'status': 'passed', 'input_sha256': data['input_sha256'], 'kernel': data['kernel'],
        'native_probe_binary_sha256': hashlib.sha256(Path(probe).read_bytes()).hexdigest(),
        'rust_probe_binary_sha256': hashlib.sha256((root/'build/rust/release/examples/nf-rust-surface-integral-probe').read_bytes()).hexdigest(),
        'control_nets_sha256': hashlib.sha256(json.dumps([[{k:p[k] for k in ('degree_u','degree_v','knots_u','knots_v','points','weights','orientation')} for p in c['patches']] for c in data['cases']],sort_keys=True,separators=(',',':')).encode()).hexdigest(),
        'native_patch_json_sha256': hashlib.sha256(native.stdout.encode()).hexdigest(),
        'integration': 'Rust-owned NURBS, composite Gauss-Legendre-8 split at surface knots; oriented xy divergence flux',
        'synthetic_checks': 'quarter-cylinder/frustum/reversed orientation, nonunit parameter domains; analytic 1e-10',
        'comparison_budgets': {'volume_mm3': 0.02/12, 'area_mm2': 0.05/12, 'quadrature_refinement_mm3_mm2': 1e-6},
        'measurement_error_bounds': None, 'correspondence_contract': 'automatic; fixed authored curve parameters are not constrained',
        'verification_scope': 'generated control nets, rectangular trim coverage, horizontal elliptic caps, section interpolation, numerical mass/area, retained B-rep bytes',
        'sampling_scope': '5x5 point/derivative samples per patch; correspondence maxima are sampled values, not global error bounds',
        'unchecked': ['constrained authored point correspondence','independent B-rep topology','full Eclipse mesh/latency acceptance','assembly','physics'],
        'retained_restore_and_source': 'passed', 'cases': reports}
    if folder is not None:
        folder.mkdir(parents=True)
        (folder/'native-patches.json').write_text(native.stdout)
        (folder/'rust-results.txt').write_text(rust.stdout)
        (folder/'verification.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print(json.dumps(receipt))


if __name__ == '__main__':
    main()
