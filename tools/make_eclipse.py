#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
"""Reproduce the v1 Eclipse headless stress fixture from the Nocturne component family."""
import argparse
import json
import math
from pathlib import Path

root = Path(__file__).resolve().parents[1]
part = json.loads((root / 'examples/modeling/nocturne.nightfall').read_text())
part['meta'] = {
    'name': 'Eclipse — four-station robotic workcell',
    'doc': 'Headless exact-solid stress fixture: four lofted manipulators, twelve twisted loft vanes, cubic swept harnesses, turned rotor, drilled flanges, chamfered hardware and an open spoke foundation. Geometric study; no assembly joints, collision, actuator, strength or physics validation. See issue #225.'}
part['params'] += [
    {'name': 'arm_count', 'value': 4, 'min': 1, 'max': 4},
    {'name': 'cell_radius', 'value': '500 mm', 'min': '480 mm', 'max': '560 mm'},
    {'name': 'vane_count', 'value': 12, 'min': 6, 'max': 16},
    {'name': 'rotor_waist', 'value': 1, 'min': 0.85, 'max': 1.15}]
features = part['features']
robot_bodies = ['pedestal', 'cooling_fins', 'shoulder_housing', 'shoulder_covers',
                'shoulder_fasteners', 'elbow_housing', 'elbow_covers', 'elbow_fasteners',
                'wrist_housing', 'wrist_covers', 'wrist_fasteners', 'upper_links', 'forearm',
                'cable_harness', 'harness_base_anchor', 'harness_wrist_anchor', 'tool_neck',
                'tool_flange', 'gripper_palm', 'gripper_jaws', 'palm_fasteners']

def feature(name, kind, **inputs):
    features.append({'id': name, 'type': kind, 'inputs': inputs})
    return name

def sketch(name, origin, entities, normal='z', x='x'):
    return feature(name, 'sketch', plane={'origin': origin, 'normal': normal, 'x_direction': x},
                   entities=entities, constraints=[])

def circle(name, center, radius):
    return {'id': name, 'type': 'circle', 'center': center, 'radius': radius}

def ring(outer, inner, center=(0, 0)):
    return [circle('outer', list(center), outer), circle('bore', list(center), inner)]

def extrude(name, profile, distance, body=None, cut=None):
    result = {'cut': cut} if cut else {'new': body, 'body_type': 'brep'}
    return feature(name, 'extrude', profile={'sketch': profile}, distance=distance, result=result)

def radial(name, bodies, count):
    return feature(name, 'pattern_circular', bodies=bodies, count=count, angle='360 deg',
                   axis={'origin': ['cell_radius', 0, 0], 'direction': 'z'})

def bevel(name, source, distance):
    feature(name, 'chamfer', edges={'edges_of': {'created_by': source, 'role': 'cap_end', 'expect': 'one'}}, distance=distance)

# Preserve the complete four-robot fixture within the 64-instance per-feature limit.
# The initial single 21-body operation was rejected (21 * 4 > 64).
radial('cell_manipulators_a', robot_bodies[:16], 'arm_count')
radial('cell_manipulators_b', robot_bodies[16:], 'arm_count')
plane = ['cell_radius', 0, -28]
p = sketch('cell_floor_section', plane, ring('cell_radius + 120 mm', 'cell_radius + 40 mm'))
extrude('cell_floor_blank', p, 20, 'cell_floor')
p = sketch('cell_spoke_section', plane, [{'id': 'spoke', 'type': 'rect', 'corner': [110, -50], 'size': ['cell_radius + 80 mm - 110 mm', 100]}])
extrude('cell_spoke_blank', p, 20, 'cell_spokes')
radial('cell_spoke_array', ['cell_spokes'], 4)
p = sketch('cell_plinth_section', plane, [circle('plinth', [0, 0], 150)])
extrude('cell_plinth_blank', p, 20, 'cell_plinth')
feature('cell_foundation_union', 'boolean', op='union', target='cell_floor', tools=['cell_spokes', 'cell_plinth'])

p = sketch('cell_station_section', [0, 0, -8], ring(90, 14) + [circle(f'bolt_{i}', [x, y], 5) for i, (x, y) in enumerate([(-48,-36),(48,-36),(48,36),(-48,36)])])
extrude('cell_station_pad', p, 8, 'cell_station_pads')
radial('cell_station_array', ['cell_station_pads'], 'arm_count')
p = sketch('cell_stand_section', ['cell_radius', 0, -8], ring(85, 20))
extrude('cell_stand', p, 52, 'cell_stand')

for label, height, outer, bore, bolt_radius, hole in [('lower',44,150,20,142,3.2), ('upper',182,130,45,120,2.5)]:
    entities = ring(outer, bore)
    entities += [circle(f'hole_{i}', [bolt_radius * math.cos(i * math.tau / 24), bolt_radius * math.sin(i * math.tau / 24)], hole) for i in range(24)]
    p = sketch(f'cell_{label}_flange_section', ['cell_radius', 0, height], entities)
    extrude(f'cell_{label}_flange', p, 8, f'cell_{label}_flange')

p = sketch('cell_rotor_section', ['cell_radius', 0, 0], [{'id': 'turned', 'type': 'polygon', 'points': [[20,52],[48,52],[48,58],[36,68],[30,168],[44,178],[44,182],[20,182]]}], normal=[0,-1,0])
feature('cell_turned_rotor', 'revolve', profile={'sketch': p}, axis={'origin': ['cell_radius', 0, 0], 'direction': 'z'}, angle='360 deg', result={'new': 'cell_rotor', 'body_type': 'brep'})

sections = []
for label, z, radius, angle, a, b in [('root',52,100,0,28,8), ('waist',117,116,15,'36 mm * rotor_waist','7 mm * rotor_waist'), ('tip',182,100,30,22,5)]:
    c, s = math.cos(math.radians(angle)), math.sin(math.radians(angle))
    origin = [f'cell_radius + ({radius*c:.15g} mm)', radius*s, z]
    p = sketch(f'cell_vane_{label}', origin, [{'id': 'rim', 'type': 'ellipse', 'center': [0,0], 'radii': [a,b]}], x=[c,s,0])
    sections.append({'sketch': p})
feature('cell_twisted_vane', 'loft', profiles=sections, result={'new': 'cell_vanes', 'body_type': 'brep'})
radial('cell_rotor_vanes', ['cell_vanes'], 'vane_count')

for label, z, offset, outer, bore, height, chamfer, count in [('lower',52,142,5,2.3,4,.5,24), ('upper',190,120,4,1.8,3,.4,24), ('deck',-8,610,6,2.8,8,1,32)]:
    offset = 'cell_radius + 110 mm' if label == 'deck' else offset
    p = sketch(f'cell_{label}_bolt_section', ['cell_radius', 0, z], ring(outer, bore, (offset, 0)))
    source = extrude(f'cell_{label}_bolt_head', p, height, f'cell_{label}_bolts')
    bevel(f'cell_{label}_bolt_bevel', source, chamfer)
    radial(f'cell_{label}_bolt_array', [f'cell_{label}_bolts'], count)

origin = ['cell_radius + 300 mm', 0, -8]
route = sketch('cell_cable_route', origin, [{'id': 'path', 'type': 'spline', 'degree': 3, 'knots': [0,0,0,0,1,1,1,1], 'control_points': [[0,0],[0,-90],[-70,-150],[-140,-82]]}], normal='y')
p = sketch('cell_cable_section', origin, [circle('jacket', [0,0], 4)])
feature('cell_cable_sweep', 'sweep', profile={'sketch': p}, path={'sketch': route, 'entity': 'path'}, result={'new': 'cell_utility_cables', 'body_type': 'brep'})
radial('cell_cable_array', ['cell_utility_cables'], 4)

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('output', nargs='?', type=Path, default=root / 'examples/stress/eclipse.nightfall')
parser.add_argument('--check', action='store_true', help='compare generated bytes without writing')
args = parser.parse_args()
encoded = (json.dumps(part, indent=2, ensure_ascii=False) + '\n').encode()
if args.check:
    if args.output.read_bytes() != encoded:
        parser.exit(1, f'{args.output}: stale Eclipse fixture; regenerate with tools/make_eclipse.py\n')
else:
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(encoded)
print(f'{args.output}: {len(features)} features' + (' verified' if args.check else ''))
