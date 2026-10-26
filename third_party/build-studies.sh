#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
#
# Fetch and build the RFC 0014 study stack as arm64 shared libraries into third_party/install:
#   Netgen 6.2.2604 (LGPL-2.1, dynamic only): tetrahedral meshing of OCCT shapes, built against
#     the pinned OCCT, without GUI, Python, MPI or native-arch tuning.
#   MFEM 4.10 (BSD-3): finite elements, serial, with no external solvers.
# Run third_party/build-occt.sh first.
#
# Usage: third_party/build-studies.sh [--jobs N]

set -euo pipefail

# Pinned upstream. Bump only through a PR that updates third_party/README.md.
readonly NETGEN_URL="https://github.com/NGSolve/netgen.git"
readonly NETGEN_TAG="v6.2.2604"
readonly NETGEN_COMMIT="3ee489c7d58fdbc2a6708cca3cbaefaae506dc17"
readonly MFEM_URL="https://github.com/mfem/mfem.git"
readonly MFEM_TAG="v4.10"
readonly MFEM_COMMIT="d964264cdb9a13e94a201b6c236c7721e0c8765f"

jobs="$(sysctl -n hw.ncpu)"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --jobs)
      if [[ $# -lt 2 || ! "$2" =~ ^[1-9][0-9]*$ ]]; then
        echo "--jobs needs a positive integer" >&2; exit 2
      fi
      jobs="$2"; shift 2 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

root="$(cd "$(dirname "$0")/.." && pwd)"
occt="${NF_OCCT_INSTALL_DIR:-$root/third_party/install/occt-7.9.3}"
if [[ ! -f "$occt/.nf-occt-commit" ]]; then
  echo "OCCT not found at $occt; run third_party/build-occt.sh first." >&2
  exit 1
fi
occt_stamp="$(cat "$occt/.nf-occt-commit")"

# fetch <url> <tag> <commit> <dir>: a clean shallow checkout at exactly <commit>.
fetch() {
  if [[ ! -d "$4/.git" ]]; then
    git clone --quiet --depth 1 --branch "$2" "$1" "$4"
  fi
  if [[ "$(git -C "$4" rev-parse HEAD)" != "$3" ]]; then
    echo "$4 is not at $3 ($2); remove it and run again." >&2
    exit 1
  fi
  if [[ -n "$(git -C "$4" status --porcelain --untracked-files=no)" ]]; then
    echo "$4 has local changes; remove it and run again." >&2
    exit 1
  fi
}

# Netgen: the stamp records its commit and the OCCT build it links.
netgen_src="$root/third_party/src/netgen"
netgen_prefix="$root/third_party/install/netgen-6.2.2604"
netgen_stamp="$NETGEN_COMMIT-occt-$occt_stamp"
if [[ -f "$netgen_prefix/.nf-netgen-commit" && "$(cat "$netgen_prefix/.nf-netgen-commit")" == "$netgen_stamp" ]]; then
  echo "Netgen 6.2.2604 already installed in $netgen_prefix"
else
  fetch "$NETGEN_URL" "$NETGEN_TAG" "$NETGEN_COMMIT" "$netgen_src"
  build="$root/build/third_party/netgen-6.2.2604"
  rm -rf "$netgen_prefix"
  cmake -S "$netgen_src" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
    -DCMAKE_INSTALL_PREFIX="$netgen_prefix" \
    -DUSE_SUPERBUILD=OFF -DUSE_GUI=OFF -DUSE_PYTHON=OFF -DUSE_MPI=OFF -DUSE_OCC=ON \
    -DUSE_NATIVE_ARCH=OFF -DUSE_JPEG=OFF -DUSE_MPEG=OFF -DUSE_CGNS=OFF \
    -DENABLE_UNIT_TESTS=OFF -DBUILD_STUB_FILES=OFF \
    -DNG_INSTALL_DIR_BIN=bin -DNG_INSTALL_DIR_LIB=lib -DNG_INSTALL_DIR_INCLUDE=include \
    -DNG_INSTALL_DIR_CMAKE=lib/cmake/netgen -DNG_INSTALL_DIR_RES=share \
    -DOpenCASCADE_DIR="$occt/lib/cmake/opencascade" -DCMAKE_PREFIX_PATH="$occt"
  cmake --build "$build" --parallel "$jobs"
  cmake --install "$build"
  printf '%s\n' "$netgen_stamp" > "$netgen_prefix/.nf-netgen-commit"
fi

mfem_src="$root/third_party/src/mfem"
mfem_prefix="$root/third_party/install/mfem-4.10"
if [[ -f "$mfem_prefix/.nf-mfem-commit" && "$(cat "$mfem_prefix/.nf-mfem-commit")" == "$MFEM_COMMIT" ]]; then
  echo "MFEM 4.10 already installed in $mfem_prefix"
else
  fetch "$MFEM_URL" "$MFEM_TAG" "$MFEM_COMMIT" "$mfem_src"
  build="$root/build/third_party/mfem-4.10"
  rm -rf "$mfem_prefix"
  cmake -S "$mfem_src" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
    -DCMAKE_INSTALL_PREFIX="$mfem_prefix" -DBUILD_SHARED_LIBS=ON \
    -DMFEM_USE_MPI=OFF -DMFEM_USE_ZLIB=OFF -DMFEM_ENABLE_EXAMPLES=OFF \
    -DMFEM_ENABLE_MINIAPPS=OFF -DMFEM_ENABLE_TESTING=OFF
  cmake --build "$build" --parallel "$jobs"
  cmake --install "$build"
  printf '%s\n' "$MFEM_COMMIT" > "$mfem_prefix/.nf-mfem-commit"
fi
