#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
#
# Fetch NVIDIA usd-brep (SMLib) at the pinned commit into third_party/src/usd-brep.
# Git LFS fixtures are skipped: the SM_API tests and our builds don't need them.
#
# Usage: third_party/fetch-smlib.sh

set -euo pipefail

# Pinned upstream: usd-brep 1.0.0. Bump only through a PR that updates third_party/README.md.
readonly URL="https://github.com/NVIDIA-Omniverse/usd-brep.git"
readonly COMMIT="c9c5979f04cea8c38213004d4f04df0dcfb40d05"

root="$(cd "$(dirname "$0")/.." && pwd)"
dest="$root/third_party/src/usd-brep"

if [[ ! -d "$dest/.git" ]]; then
  mkdir -p "$(dirname "$dest")"
  GIT_LFS_SKIP_SMUDGE=1 git clone --quiet --no-checkout "$URL" "$dest"
fi

cd "$dest"
if ! git cat-file -e "$COMMIT^{commit}" 2>/dev/null; then
  GIT_LFS_SKIP_SMUDGE=1 git fetch --quiet origin
fi
# Refuse to reuse a checkout with local edits: results must come from the pinned source.
if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
  echo "usd-brep checkout at $dest has local modifications:" >&2
  git status --short --untracked-files=no >&2
  echo "Stash or discard them (git -C \"$dest\" checkout -- .) and run again." >&2
  exit 1
fi
GIT_LFS_SKIP_SMUDGE=1 git -c advice.detachedHead=false checkout --quiet "$COMMIT"

actual="$(git rev-parse HEAD)"
if [[ "$actual" != "$COMMIT" ]]; then
  echo "expected $COMMIT, got $actual" >&2
  exit 1
fi
echo "usd-brep at $COMMIT in $dest"
