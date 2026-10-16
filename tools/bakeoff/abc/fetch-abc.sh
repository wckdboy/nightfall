#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
#
# Download ABC chunk 0000 (stats + STEP) into the git-ignored third_party/src/abc/, verify
# the archives against NYU's published MD5s, extract the models listed in the manifest and
# verify each one's SHA-256. The models stay local: see README.md for the terms.
#
# Usage: tools/bakeoff/abc/fetch-abc.sh [--all]
#   --all   extract the whole chunk (needed only to regenerate the manifest)
#   NF_ABC_DIR overrides the download directory. Requires 7zz (brew install sevenzip).

set -euo pipefail

readonly STAT_URL="https://archive.nyu.edu/rest/bitstreams/89086/retrieve"
readonly STAT_MD5="94dc4c00aac0b7ba4c542cd6563f271f"
readonly STEP_URL="https://archive.nyu.edu/rest/bitstreams/88598/retrieve"
readonly STEP_MD5="695388be7a278c7798c8c8ae239772ac"

all=0
[[ "${1:-}" == "--all" ]] && all=1

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../../.." && pwd)"
dir="${NF_ABC_DIR:-$root/third_party/src/abc}"
manifest="$here/manifest-v1.csv"
command -v 7zz >/dev/null || { echo "7zz not found: brew install sevenzip" >&2; exit 1; }
mkdir -p "$dir"

fetch() {  # url md5 file
  local url="$1" md5="$2" file="$dir/$3"
  if [[ ! -f "$file" || "$(md5 -q "$file")" != "$md5" ]]; then
    echo "downloading $3"
    curl -fL --retry 3 -o "$file.part" "$url"
    mv "$file.part" "$file"
  fi
  if [[ "$(md5 -q "$file")" != "$md5" ]]; then
    echo "$3: MD5 mismatch (expected $md5)" >&2
    exit 1
  fi
}

fetch "$STAT_URL" "$STAT_MD5" abc_0000_stat_v00.7z
fetch "$STEP_URL" "$STEP_MD5" abc_0000_step_v00.7z

7zz x -y -bso0 -o"$dir/stat" "$dir/abc_0000_stat_v00.7z"
if [[ $all -eq 1 ]]; then
  7zz x -y -bso0 -o"$dir/step" "$dir/abc_0000_step_v00.7z"
else
  list="$(mktemp)"
  trap 'rm -f "$list"' EXIT
  tail -n +2 "$manifest" | cut -d, -f1,2 | tr , / > "$list"
  7zz x -y -bso0 -o"$dir/step" "$dir/abc_0000_step_v00.7z" @"$list"
fi

bad=0
while IFS=, read -r model file _faces _bin hash; do
  path="$dir/step/$model/$file"
  if [[ ! -f "$path" ]]; then
    echo "missing: $model/$file" >&2; bad=1
  elif [[ "$(shasum -a 256 "$path" | cut -d' ' -f1)" != "$hash" ]]; then
    echo "SHA-256 mismatch: $model/$file" >&2; bad=1
  fi
done < <(tail -n +2 "$manifest")
[[ $bad -eq 0 ]] || exit 1
echo "$(($(wc -l < "$manifest") - 1)) ABC models verified in $dir/step"
