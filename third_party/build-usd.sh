#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
#
# Fetch OpenUSD at the pinned release and build a C++-only install (no Python, no imaging)
# into third_party/install/usd-<version>, using OpenUSD's own build_usd.py (which fetches
# its dependencies, e.g. oneTBB). Pinned to the release usd-brep 1.0.0 builds against.
# The OpenUSD commit is pinned; the dependency versions are the ones build_usd.py at that
# commit pins, so the CI cache key (this script's hash) covers them only through the commit.
#
# Usage: third_party/build-usd.sh [--jobs N]
#   NF_USD_SOURCE_DIR / NF_USD_INSTALL_DIR override the default locations.

set -euo pipefail

readonly URL="https://github.com/PixarAnimationStudios/OpenUSD.git"
readonly TAG="v25.11"
readonly COMMIT="363a7c8da8d1937072a5f0989e91faf72eb1fa76"
readonly VERSION="25.11"

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
src="${NF_USD_SOURCE_DIR:-$root/third_party/src/openusd}"
prefix="${NF_USD_INSTALL_DIR:-$root/third_party/install/usd-$VERSION}"
work="$root/build/third_party/usd-$VERSION"

if [[ -f "$prefix/.nf-usd-commit" && "$(cat "$prefix/.nf-usd-commit")" == "$COMMIT" ]]; then
  echo "OpenUSD $VERSION already installed in $prefix"
  exit 0
fi

if [[ ! -d "$src/.git" ]]; then
  git clone --quiet --depth 1 --branch "$TAG" "$URL" "$src"
fi
if [[ "$(git -C "$src" rev-parse HEAD)" != "$COMMIT" ]]; then
  echo "OpenUSD checkout at $src is not at $COMMIT ($TAG); remove it and run again." >&2
  exit 1
fi
if [[ -n "$(git -C "$src" status --porcelain --untracked-files=no)" ]]; then
  echo "OpenUSD checkout at $src has local modifications; discard them and run again." >&2
  exit 1
fi

# Match Nightfall's deployment target (CMakePresets.json) instead of the host OS version.
export MACOSX_DEPLOYMENT_TARGET=15.0
mkdir -p "$work"
python3 "$src/build_scripts/build_usd.py" "$prefix" \
  --build "$work/build" --src "$work/deps-src" \
  --build-variant release --jobs "$jobs" \
  --no-python --no-imaging --no-usdview --no-materialx --no-examples --no-tutorials \
  --no-tools --no-docs --no-tests

echo "$COMMIT" > "$prefix/.nf-usd-commit"
echo "OpenUSD $VERSION installed in $prefix"
