# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
"""The published schema and native decoder share this corpus (RFC 0005)."""
import json
from pathlib import Path

from jsonschema import Draft202012Validator

root = Path(__file__).resolve().parent.parent
schema = json.loads((root / "schema/part-v1.schema.json").read_text())
Draft202012Validator.check_schema(schema)
validator = Draft202012Validator(schema)
cases = json.loads((root / "tests/schema-corpus.json").read_text())
for case in cases:
    errors = list(validator.iter_errors(case["document"]))
    assert (not errors) == case["schema_valid"], (case["name"], [e.message for e in errors])
print(f"Published schema: {len(cases)} corpus cases passed")
