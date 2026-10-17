// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "schema.hpp"

#include <algorithm>
#include <charconv>
#include <unordered_map>

namespace nf::ir::detail {
namespace {
struct Symbol {
  std::size_t feature;
  std::string type;
};
using Symbols = std::unordered_map<std::string, Symbol>;

std::size_t index(std::string_view pointer, std::string_view marker) {
  const auto start = pointer.find(marker);
  if (start == std::string_view::npos) throw std::logic_error("invalid schema link location");
  const auto digits = pointer.substr(start + marker.size());
  const auto slash = digits.find('/');
  if (slash == std::string_view::npos) throw std::logic_error("invalid schema link index");
  std::size_t result = 0;
  const auto parsed = std::from_chars(digits.data(), digits.data() + slash, result);
  if (parsed.ec != std::errc() || parsed.ptr != digits.data() + slash)
    throw std::logic_error("invalid schema link index");
  return result;
}
}  // namespace

Errors check_links(const Document& d, std::span<const Link> links) {
  Errors errors;
  const auto fail = [&](std::string code, const std::string& pointer, std::string message) {
    if (errors.size() < 128 && std::ranges::none_of(errors, [&](const auto& e) {
          return e.code == code && e.pointer == pointer;
        }))
      errors.push_back({std::move(code), std::move(message), pointer, std::nullopt});
  };
  Symbols features, bodies, entities;
  features.reserve(d.features.size());
  for (std::size_t i = 0; i < d.features.size(); ++i)
    features.emplace(d.features[i].id, Symbol{i, d.features[i].type});
  std::unordered_map<std::string, std::string> references;
  for (const auto& ref : d.references) references.emplace(ref.id, ref.kind);
  // Gather all definitions first so a forward use is distinguishable from a missing ID.
  for (const auto& link : links) {
    if (!link.definition) continue;
    const auto i = index(link.pointer, "/features/");
    if (link.kind == "body") {
      if (!bodies.emplace(link.target, Symbol{i, "body"}).second)
        fail("schema", link.pointer, "duplicate body ID \"" + link.target + "\"");
    } else if (link.kind == "entity") {
      const auto j = index(link.pointer, "/entities/");
      const auto& entity = d.features.at(i).inputs.find("entities")->if_array()->at(j);
      const auto name = d.features[i].id + "/" + link.target;
      if (!entities.emplace(name, Symbol{i, *entity.find("type")->if_string()}).second)
        fail("schema", link.pointer, "duplicate sketch entity ID \"" + link.target + "\"");
    } else
      throw std::logic_error("unknown schema definition kind");
  }
  const auto resolve = [&](const Symbols& symbols, const std::string& name, const std::string& p,
                           std::size_t consumer, bool local, std::string_view required_type) {
    const auto found = symbols.find(name);
    if (found == symbols.end()) {
      fail("unknown_id", p, "unknown ID \"" + name + "\"");
      return;
    }
    if (found->second.feature > consumer || (!local && found->second.feature == consumer)) {
      fail("forward_reference", p, "\"" + name + "\" must refer to an earlier feature");
      return;
    }
    if (!required_type.empty() && found->second.type != required_type)
      fail("schema", p, "\"" + name + "\" must name a " + std::string(required_type));
  };
  for (const auto& link : links) {
    if (link.definition) continue;
    const auto i = index(link.pointer, "/features/");
    if (link.kind == "reference" || link.kind == "svg") {
      const auto found = references.find(link.target);
      if (found == references.end())
        fail("unknown_id", link.pointer, "unknown reference \"" + link.target + "\"");
      else if (link.kind == "svg" && found->second != "svg")
        fail("schema", link.pointer, "expected an SVG reference");
    } else if (link.kind == "feature" || link.kind == "sketch") {
      resolve(features, link.target, link.pointer, i, false, link.kind == "sketch" ? "sketch" : "");
    } else if (link.kind == "body") {
      resolve(bodies, link.target, link.pointer, i, false, "");
    } else if (link.kind == "entity") {
      resolve(entities, link.target, link.pointer, i, false, "");
    } else if (link.kind == "local_entity") {
      resolve(entities, d.features.at(i).id + "/" + link.target, link.pointer, i, true, "");
    } else if (link.kind == "sketch_entity" || link.kind == "sketch_line" ||
               link.kind == "sketch_point") {
      const auto sketch = features.find(link.target.substr(0, link.target.find('/')));
      // The object's /sketch link diagnoses these errors; avoid a misleading entity error.
      if (sketch == features.end() || sketch->second.type != "sketch" ||
          sketch->second.feature >= i)
        continue;
      const auto type = link.kind == "sketch_line"    ? "line"
                        : link.kind == "sketch_point" ? "point"
                                                      : "";
      resolve(entities, link.target, link.pointer + "/entity", i, false, type);
    } else
      throw std::logic_error("unknown schema reference kind");
  }
  return errors;
}
}  // namespace nf::ir::detail
