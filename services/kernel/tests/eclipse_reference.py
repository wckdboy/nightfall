#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
"""Bind independent Rust vane integration to the full headless Eclipse acceptance."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def binding(full, seed):
    for key in ('format', 'version', 'units', 'params', 'references', 'history_end'):
        require(full[key] == seed[key], 'vane subgraph mismatch: '+key)
    ids = [f['id'] for f in seed['features']]
    require(len(ids) == 4 and ids[-1] == 'cell_twisted_vane', 'vane seed coverage')
    require([f for f in full['features'] if f['id'] in ids] == seed['features'],
            'vane subgraph mismatch: feature inputs')
    pattern = next(f for f in full['features'] if f['id'] == 'cell_rotor_vanes')
    require(pattern == {'id': 'cell_rotor_vanes', 'type': 'pattern_circular',
            'inputs': {'bodies': ['cell_vanes'], 'count': 'vane_count', 'angle': '360 deg',
                       'axis': {'origin': ['cell_radius', 0, 0], 'direction': 'z'}}},
            'vane pattern contract mismatch')
    require(next(p['value'] for p in full['params'] if p['name'] == 'vane_count') == 12,
            'vane instance coverage')
    return ids


def run(command, root):
    result = subprocess.run(command, cwd=root, stdout=subprocess.PIPE, text=True)
    if result.returncode:
        raise RuntimeError('acceptance stage failed: '+str(command[0]))
    return json.loads(result.stdout)


def main():
    require(len(sys.argv) in (4, 5),
            'usage: eclipse_reference.py eclipse-probe loft-probe repo [new-output-directory]')
    probe, loft_probe = (str(Path(arg).resolve()) for arg in sys.argv[1:3])
    root = Path(sys.argv[3]).resolve()
    folder = Path(sys.argv[4]).resolve() if len(sys.argv) == 5 else None
    if folder is not None:
        require(not folder.exists(), 'output directory must be new')
    fixture = root/'examples/stress/eclipse.nightfall'
    seed = root/'services/kernel/tests/fixtures/eclipse_vane.nightfall'
    source, seed_source = fixture.read_bytes(), seed.read_bytes()
    feature_ids = binding(json.loads(source), json.loads(seed_source))
    if folder is not None:
        folder.mkdir(parents=True)
    command = [sys.executable, str(root/'services/kernel/tests/loft_rust_reference.py'),
               loft_probe, str(root)]
    if folder is not None:
        command.append(str(folder/'loft-reference'))
    reference = run(command, root)
    require(reference['status'] == 'passed', 'independent vane reference failed')
    require(reference['input_sha256'] == hashlib.sha256(seed_source).hexdigest(),
            'seed source binding mismatch')
    require([c['waist'] for c in reference['cases']] == [1, 1.05, 1], 'vane edit coverage')
    oracle = {'status': 'passed', 'fixture_sha256': hashlib.sha256(source).hexdigest(),
              'kernel': reference['kernel'], 'seed_feature_ids': feature_ids,
              'seed_subgraph_inputs_parameters_units': 'equal to full fixture',
              'copies': 12, 'default_seed_volume_mm3': reference['cases'][0]['rust_volume_mm3'],
              'edited_seed_volume_mm3': reference['cases'][1]['rust_volume_mm3'],
              'independent_reference': reference,
              'scope': 'Rust integration of native-generated seed surfaces; twelve rigid copies',
              'unchecked': ['independent B-rep topology', 'constrained authored point correspondence'],
              'driver_source_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    with tempfile.TemporaryDirectory(prefix='nightfall-eclipse-') as temporary:
        oracle_path = Path(temporary)/'vane-oracle.json'
        oracle_path.write_text(json.dumps(oracle, indent=2)+'\n')
        command = [probe, str(fixture), str(oracle_path)]
        if folder is not None:
            command.append(str(folder/'model'))
        receipt = run(command, root)
        require(receipt['status'] == 'passed', 'full acceptance failed')
        require(receipt['vane_oracle'] == oracle, 'full receipt oracle mismatch')
        require(receipt['fixture_sha256'] == oracle['fixture_sha256'], 'full source mismatch')
    require(fixture.read_bytes() == source and seed.read_bytes() == seed_source,
            'fixture source changed')
    if folder is not None:
        (folder/'verification.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print(json.dumps(receipt))


if __name__ == '__main__':
    main()
