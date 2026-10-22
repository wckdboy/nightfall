# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
"""The published schemas and native decoder share these corpora (RFC 0005, RFC 0013)."""
import json
from pathlib import Path

from jsonschema import Draft202012Validator

root = Path(__file__).resolve().parent.parent


def validator(version):
    schema = json.loads((root / f"schema/part-v{version}.schema.json").read_text())
    Draft202012Validator.check_schema(schema)
    return Draft202012Validator(schema)


def migrated(document):
    """Migration 1 -> 2 changes only the version (RFC 0013)."""
    if isinstance(document, dict) and document.get("version") == 1:
        return {**document, "version": 2}
    return document


def check(validator, cases, transform=lambda d: d):
    for case in cases:
        errors = list(validator.iter_errors(transform(case["document"])))
        assert (not errors) == case["schema_valid"], (case["name"], [e.message for e in errors])


v1, v2 = validator(1), validator(2)
cases = json.loads((root / "tests/schema-corpus.json").read_text())
check(v1, cases)
check(v2, cases, migrated)
print(f"Published schemas: {len(cases)} v1 corpus cases passed as v1 and migrated to v2")
assembly = json.loads((root / "tests/schema-corpus-v2.json").read_text())
check(v2, assembly)
print(f"Published schema v2: {len(assembly)} assembly corpus cases passed")

parts = sorted((root.parent.parent / "examples/parts").glob("*.nightfall"))
assert len(parts) == 20, f"Expected 20 reference parts, found {len(parts)}"
for part in parts:
    document = json.loads(part.read_text(encoding="utf-8"))
    for schema, value in ((v1, document), (v2, migrated(document))):
        errors = list(schema.iter_errors(value))
        assert not errors, (part.name, [e.message for e in errors])
print(f"Published schemas: {len(parts)} reference parts passed as v1 and migrated to v2")
