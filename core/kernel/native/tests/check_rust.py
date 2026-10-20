#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
"""Run safe native/kernel crates against this CMake configuration's built adapter."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

root, cargo, library, expected, target, *probes = sys.argv[1:]
rust_target = next(line.removeprefix("host: ") for line in
                   subprocess.check_output([cargo, "-vV"], text=True).splitlines()
                   if line.startswith("host: "))
env = dict(os.environ, NF_NATIVE_LIB_DIR=str(Path(library).resolve()),
           NF_NATIVE_EXPECT_OCCT=expected, CARGO_TARGET_DIR=target)
# rpaths must also reach rustdoc and downstream Rust consumers, not just this package's tests.
for variable in ("RUSTFLAGS", "RUSTDOCFLAGS"):
    encoded = "CARGO_ENCODED_" + variable
    existing = (env[encoded].split("\x1f") if encoded in env else env.get(variable, "").split())
    env[encoded] = "\x1f".join(existing + ["-D", "warnings", "-C", f"link-arg=-Wl,-rpath,{library}"])
packages = ["--workspace", "--exclude", "nightfall-math"]
base = [cargo, "clippy", *packages, "--all-targets", "--target", rust_target,
        "--profile", "dev", "--locked", "--offline",
        "--", "-D", "warnings"]
subprocess.run(base, cwd=root, env=env, check=True)
for profile in ("dev", "release"):
    subprocess.run([cargo, "test", *packages, "--target", rust_target,
                    "--profile", profile, "--locked", "--offline"],
                   cwd=root, env=env, check=True)
if expected == "1":
    for package, example in (("nightfall-native", "nf-rust-extrude"),
                             ("nightfall-kernel", "nf-kernel-extrude"),
                             ("nightfall-kernel", "nf-kernel-placement"),
                             ("nightfall-kernel", "nf-kernel-mesh"),
                             ("nightfall-kernel", "nf-kernel-step")):
        subprocess.run([cargo, "run", "-p", package, "--example", example,
                        "--target", rust_target, "--release", "--locked", "--offline"],
                       cwd=root, env=env, check=True)
    assert len(probes) == 1, "linked STEP reload probe required"
    example = Path(target) / rust_target / "release/examples/nf-kernel-step"
    with tempfile.TemporaryDirectory(prefix="nf-rust-step-") as directory:
        output = Path(directory) / "placed rational plates.step"
        subprocess.run([example, "--output", output], cwd=root, env=env, check=True)
        before = output.read_bytes()
        subprocess.run([probes[0], output], cwd=root, env=env, check=True)
        refused = subprocess.run([example, "--output", output], cwd=root, env=env,
                                 capture_output=True, text=True)
        assert refused.returncode != 0, "existing output must be refused"
        assert output.read_bytes() == before, "existing STEP output changed"
