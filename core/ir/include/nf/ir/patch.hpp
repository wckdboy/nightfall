// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// RFC 6902 JSON Patch over the feature IR (RFC 0011, Track 1): the agent edits the document
// with small, reviewable operations instead of resending it, and every failure points at the
// operation and the path that failed.

#pragma once

#include "nf/ir/document.hpp"

namespace nf::ir {

inline constexpr std::size_t kMaxPatchOperations = 1024;

/// Applies `patch` (a JSON array of RFC 6902 operations: add, remove, replace, move, copy,
/// test) to `document` with RFC 6901 pointers and returns the result. Atomic: either every
/// operation applies, in order, or the first failure is returned and nothing is changed.
/// Each error's pointer is "/patch/<index>" and its message names the operation's path.
/// `add` on an existing object member replaces it; "-" appends to an array; `move` refuses a
/// target inside its own source; `remove` and `replace` at the root are refused. Inputs are
/// never modified and the result is not validated as a part document: pass it through
/// read_document/write_document for that.
[[nodiscard]] std::expected<json::Value, Errors> apply_patch(const json::Value& document,
                                                             const json::Value& patch);

}  // namespace nf::ir
