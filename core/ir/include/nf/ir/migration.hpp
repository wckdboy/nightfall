// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// Forward migrations of part documents (RFC 0005 "Versioning and migrations"): pure functions
// from version n JSON to n + 1, kept forever and chained on load.

#pragma once

#include "nf/ir/json.hpp"

namespace nf::ir {

/// Version 1 to 2 (RFC 0013): only `version` changes, in place. Every v1 document is a v2
/// document without components, occurrences or joints, with the same feature hashes.
[[nodiscard]] json::Value migrate_1_to_2(json::Value document);

/// Applies every migration from the document's version to the current one. Documents already
/// current, newer, or without an integer version are returned unchanged for the reader to judge.
[[nodiscard]] json::Value migrate(json::Value document);

}  // namespace nf::ir
