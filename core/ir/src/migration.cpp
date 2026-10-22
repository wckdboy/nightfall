// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "nf/ir/migration.hpp"

#include "nf/ir/document.hpp"

namespace nf::ir {

json::Value migrate_1_to_2(json::Value document) {
  if (!document.if_object()) return document;
  json::Object members = *document.if_object();
  for (auto& member : members)
    if (member.key == "version") member.value = 2.0;
  return members;
}

json::Value migrate(json::Value document) {
  const auto* version = document.find("version");
  const auto n = version ? version->if_integer() : std::nullopt;
  if (!n) return document;
  if (*n == 1) document = migrate_1_to_2(std::move(document));
  static_assert(kDocumentVersion == 2, "add the new migration step here");
  return document;
}

}  // namespace nf::ir
