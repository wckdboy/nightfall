#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
"""Run safe native/kernel crates against this CMake configuration's built adapter."""
import os
from pathlib import Path
import subprocess
import sys

root, cargo, library, expected, target = sys.argv[1:]
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
                             ("nightfall-kernel", "nf-kernel-extrude")):
        subprocess.run([cargo, "run", "-p", package, "--example", example,
                        "--target", rust_target, "--release", "--locked", "--offline"],
                       cwd=root, env=env, check=True)
