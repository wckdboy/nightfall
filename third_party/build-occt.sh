#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
#
# Fetch OpenCASCADE at the pinned release, build the modeling + data-exchange toolkits as
# arm64 shared libraries, and install them into third_party/install/occt-<version>.
# No Draw, Tcl/Tk, FreeType or OpenGL. Used by the RFC 0001 bake-off (-DNF_WITH_OCCT=ON).
#
# Usage: third_party/build-occt.sh [--jobs N]
#   NF_OCCT_SOURCE_DIR / NF_OCCT_INSTALL_DIR override the default locations.

set -euo pipefail

# Pinned upstream: OCCT 7.9.3. Bump only through a PR that updates third_party/README.md.
readonly URL="https://github.com/Open-Cascade-SAS/OCCT.git"
readonly TAG="V7_9_3"
readonly COMMIT="a016080bf6738d6aeae020badee4e888ad1540a5"
readonly VERSION="7.9.3"

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
src="${NF_OCCT_SOURCE_DIR:-$root/third_party/src/occt}"
prefix="${NF_OCCT_INSTALL_DIR:-$root/third_party/install/occt-$VERSION}"
# Keyed on the source path so a different NF_OCCT_SOURCE_DIR never reuses a CMake cache.
build="$root/build/third_party/occt-$VERSION-$(printf %s "$src" | shasum -a 256 | cut -c1-8)"

if [[ -f "$prefix/.nf-occt-commit" && "$(cat "$prefix/.nf-occt-commit")" == "$COMMIT" ]]; then
  echo "OCCT $VERSION already installed in $prefix"
  exit 0
fi

if [[ ! -d "$src/.git" ]]; then
  git clone --quiet --depth 1 --branch "$TAG" "$URL" "$src"
fi
if [[ "$(git -C "$src" rev-parse HEAD)" != "$COMMIT" ]]; then
  echo "OCCT checkout at $src is not at $COMMIT ($TAG); remove it and run again." >&2
  exit 1
fi
if [[ -n "$(git -C "$src" status --porcelain --untracked-files=no)" ]]; then
  echo "OCCT checkout at $src has local modifications; discard them and run again." >&2
  exit 1
fi

cmake -S "$src" -B "$build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DBUILD_LIBRARY_TYPE=Shared \
  -DBUILD_MODULE_Draw=OFF \
  -DBUILD_MODULE_DETools=OFF \
  -DBUILD_MODULE_Visualization=OFF \
  -DBUILD_MODULE_ApplicationFramework=OFF \
  -DBUILD_MODULE_DataExchange=ON \
  -DUSE_TK=OFF -DUSE_FREETYPE=OFF -DUSE_OPENGL=OFF -DUSE_GLES2=OFF -DUSE_XLIB=OFF \
  -DBUILD_DOC_Overview=OFF \
  -DINSTALL_DIR="$prefix" >/dev/null

cmake --build "$build" --parallel "$jobs"
cmake --install "$build" >/dev/null
echo "$COMMIT" > "$prefix/.nf-occt-commit"
echo "OCCT $VERSION installed in $prefix"
