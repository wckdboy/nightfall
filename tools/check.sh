#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
#
# The one local gate. CI runs exactly this script, so "check.sh passes" means
# "CI's build, test and hygiene steps pass".
#
# Usage: tools/check.sh [--preset dev|release|asan|smlib|occt|bakeoff] [--skip-format]

set -euo pipefail

preset="dev"
skip_format=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --preset) preset="$2"; shift 2 ;;
    --skip-format) skip_format=1; shift ;;
    -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

step() { printf '\n==> %s\n' "$*"; }
fail=0

step "License headers"
# Every tracked or new source file must carry an SPDX license identifier.
missing=$(git ls-files --cached --others --exclude-standard \
  '*.cpp' '*.hpp' '*.h' '*.mm' '*.swift' '*.sh' '*.py' '*CMakeLists.txt' '*.cmake' \
  | while read -r f; do
      [[ -f "$f" ]] || continue
      head -n 5 "$f" | grep -q 'SPDX-License-Identifier:' || echo "$f"
    done)
if [[ -n "$missing" ]]; then
  echo "missing SPDX-License-Identifier:"; echo "$missing" | sed 's/^/  /'
  fail=1
else
  echo "ok"
fi

step "Layering"
# AppKit/Cocoa only inside app/ (WORKFLOW.md §6): everything else must stay portable to
# iPadOS and visionOS. Catches Swift/ObjC imports, C/C++ includes and CMake framework links.
appkit=$(git ls-files --cached --others --exclude-standard \
  '*.swift' '*.m' '*.mm' '*.h' '*.hpp' '*.c' '*.cpp' '*CMakeLists.txt' '*.cmake' \
  | { grep -v '^app/' || true; } \
  | while read -r f; do
      [[ -f "$f" ]] || continue
      grep -nHE '^[[:space:]]*(@[_a-zA-Z]+[[:space:]]+)?import([[:space:]]+(typealias|struct|class|enum|protocol|let|var|func))?[[:space:]]+(AppKit|Cocoa)\b|^[[:space:]]*@import[[:space:]]+(AppKit|Cocoa)\b|#[[:space:]]*(import|include)[[:space:]]*[<"](AppKit|Cocoa)/|-framework[,[:space:]]+(AppKit|Cocoa)\b|find_library[[:space:]]*\([^)]*\b(AppKit|Cocoa)\b' "$f" || true
    done)
if [[ -n "$appkit" ]]; then
  echo "AppKit/Cocoa used outside app/:"; echo "$appkit" | sed 's/^/  /'
  fail=1
else
  echo "ok"
fi

step "Format"
if [[ $skip_format -eq 1 ]]; then
  echo "skipped (--skip-format)"
elif ! command -v clang-format >/dev/null; then
  echo "clang-format not installed; skipping locally (CI enforces it). pip install clang-format==$(cat .clang-format-version)"
elif ! clang-format --version | grep -q "version $(cat .clang-format-version)"; then
  msg="clang-format $(cat .clang-format-version) required (.clang-format-version), found: $(clang-format --version)"
  if [[ -n "${CI:-}" ]]; then echo "$msg"; fail=1; else echo "$msg; skipping locally"; fi
else
  files=$(git ls-files --cached --others --exclude-standard '*.cpp' '*.hpp' '*.h' '*.mm')
  if [[ -n "$files" ]] && ! echo "$files" | xargs clang-format --dry-run --Werror; then
    echo "run: git ls-files '*.cpp' '*.hpp' '*.h' '*.mm' | xargs clang-format -i"
    fail=1
  else
    echo "ok"
  fi
fi

step "IR schema"
python3 core/ir/schema/build_schema.py --check
schema_python="${NF_SCHEMA_PYTHON:-python3}"
if "$schema_python" -c 'import jsonschema' 2>/dev/null; then
  "$schema_python" core/ir/tests/check_schema.py
elif [[ -n "${CI:-}" ]]; then
  echo "install tools/schema-requirements.txt for the required schema check" >&2
  exit 1
else
  echo "jsonschema not installed; cross-check skipped (CI enforces it). See README.md."
fi

step "Configure ($preset)"
cmake --preset "$preset" >/dev/null

step "Build ($preset)"
cmake --build --preset "$preset"

step "Test ($preset)"
ctest --preset "$preset"

if [[ $fail -ne 0 ]]; then
  printf '\nFAILED: hygiene checks above must be fixed.\n' >&2
  exit 1
fi
printf '\nAll checks passed.\n'
