// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "schema.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <unordered_map>
#include <unordered_set>

namespace nf::ir::detail {
namespace {
using json::Value;

/// IDs a frame selector may name inside one component's features.
struct Scope {
  std::unordered_set<std::string> features, bodies, entities;
};

/// The feature index after "/features/" in a pointer local to its feature list.
std::optional<std::size_t> feature_index(std::string_view pointer) {
  const auto start = pointer.find("/features/");
  if (start == std::string_view::npos) return std::nullopt;
  const auto digits = pointer.substr(start + 10);
  std::size_t i = 0;
  const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), i);
  if (parsed.ec != std::errc()) return std::nullopt;
  return i;
}

/// Degrees of freedom a joint kind leaves free, as the dimension each value or limit takes.
std::optional<std::vector<Dimension>> freedoms(std::string_view kind) {
  if (kind == "rigid" || kind == "ball") return std::vector<Dimension>{};
  if (kind == "revolute") return std::vector{kAngle};
  if (kind == "slider") return std::vector{kLength};
  if (kind == "cylindrical" || kind == "pin_slot") return std::vector{kAngle, kLength};
  if (kind == "planar") return std::vector{kLength, kLength, kAngle};
  return std::nullopt;
}

bool plain_value(const Value& v) { return !v.is_null() && !v.if_array() && !v.if_object(); }
}  // namespace

Errors check_assembly(const Document& d, std::span<const Link> links,
                      std::span<const Link> assembly, const ParameterValues& params) {
  Errors errors;
  const auto fail = [&](std::string code, const std::string& pointer, std::string message,
                        std::optional<std::size_t> offset = std::nullopt) {
    if (errors.size() < 128)
      errors.push_back({std::move(code), std::move(message), pointer, offset});
  };
  if (d.components.empty() && d.occurrences.empty() && d.joints.empty()) return errors;
  const auto quantity = [&](const Value& v, Dimension dimension,
                            const std::string& p) -> std::optional<double> {
    auto result = evaluate_input(v, dimension, d.units, params);
    if (!result) {
      fail(result.error().code, p, result.error().message, result.error().offset);
      return std::nullopt;
    }
    return *result;
  };
  const auto vector3 = [&](const Value* v, Dimension dimension, const std::string& p) {
    if (!v || !v->if_array()) return;
    for (std::size_t i = 0; i < v->if_array()->size(); ++i)
      quantity((*v->if_array())[i], dimension, p + "/" + std::to_string(i));
  };
  const auto placement = [&](const std::optional<Value>& v, const std::string& p) {
    if (!v) return;
    vector3(v->find("translation"), kLength, p + "/translation");
    vector3(v->find("rotation"), kAngle, p + "/rotation");
  };

  // Scopes: the root features and each inline component's own features.
  std::unordered_map<std::string, std::size_t> components;
  for (std::size_t k = 0; k < d.components.size(); ++k) components.emplace(d.components[k].id, k);
  Scope root;
  std::vector<Scope> scopes(d.components.size());
  for (const auto& f : d.features) root.features.insert(f.id);
  for (std::size_t k = 0; k < d.components.size(); ++k)
    if (d.components[k].features)
      for (const auto& f : *d.components[k].features) scopes[k].features.insert(f.id);
  for (const auto& link : links) {
    if (!link.definition) continue;
    Scope* scope = nullptr;
    const std::vector<Feature>* features = nullptr;
    std::string_view local = link.pointer;
    if (local.starts_with("/features/")) {
      scope = &root;
      features = &d.features;
    } else if (local.starts_with("/components/")) {
      const auto slash = local.find('/', 12);
      std::size_t k = 0;
      std::from_chars(local.data() + 12, local.data() + slash, k);
      if (k >= d.components.size() || !d.components[k].features) continue;
      scope = &scopes[k];
      features = &*d.components[k].features;
      local = local.substr(slash);
    } else {
      continue;
    }
    const auto i = feature_index(local);
    if (!i || *i >= features->size()) continue;
    if (link.kind == "body") scope->bodies.insert(link.target);
    if (link.kind == "entity") scope->entities.insert((*features)[*i].id + "/" + link.target);
  }

  const auto overrides = [&](const json::Object& values, const std::string& p, bool known) {
    for (const auto& m : values) {
      if (m.key.starts_with("x-")) continue;
      const auto pointer = p + "/" + m.key;
      const auto found = params.find(m.key);
      if (known && found == params.end()) {
        fail("unknown_id", pointer, "unknown parameter \"" + m.key + "\"");
        continue;
      }
      const auto value = evaluate_value(m.value, params);
      if (!value)
        fail(value.error().code, pointer, value.error().message, value.error().offset);
      else if (known && value->dimension != found->second.dimension)
        fail("unit", pointer, "override must keep the dimension of parameter \"" + m.key + "\"");
    }
  };
  std::unordered_set<std::string> references;
  for (const auto& ref : d.references) references.insert(ref.id);
  for (std::size_t k = 0; k < d.components.size(); ++k) {
    const auto& c = d.components[k];
    const auto p = "/components/" + std::to_string(k);
    if (c.reference && !references.contains(*c.reference))
      fail("unknown_id", p + "/reference", "unknown reference \"" + *c.reference + "\"");
    // Inline components share the document's parameters; a referenced file's are not read.
    overrides(c.params, p + "/params", c.features.has_value());
  }
  std::unordered_map<std::string, std::size_t> occurrences;
  for (std::size_t i = 0; i < d.occurrences.size(); ++i) {
    const auto& o = d.occurrences[i];
    const auto p = "/occurrences/" + std::to_string(i);
    occurrences.emplace(o.id, i);
    const auto component = components.find(o.component);
    if (o.component != "root" && component == components.end()) {
      fail("unknown_id", p + "/component", "unknown component \"" + o.component + "\"");
      continue;
    }
    overrides(o.params, p + "/params",
              o.component == "root" || d.components[component->second].features.has_value());
    placement(o.placement, p + "/placement");
  }

  // The scope a joint end's frame resolves in: nullptr for referenced or unknown components.
  const auto scope_of = [&](const std::string& occurrence) -> const Scope* {
    const auto found = occurrences.find(occurrence.substr(0, occurrence.find('/')));
    if (found == occurrences.end()) return nullptr;
    const auto& component = d.occurrences[found->second].component;
    if (component == "root") return &root;
    const auto k = components.find(component);
    if (k == components.end() || !d.components[k->second].features) return nullptr;
    return &scopes[k->second];
  };
  for (std::size_t j = 0; j < d.joints.size(); ++j) {
    const auto& joint = d.joints[j];
    const auto p = "/joints/" + std::to_string(j);
    for (const auto* end : {&joint.a, &joint.b}) {
      const auto q = p + (end == &joint.a ? "/a" : "/b");
      const auto first = end->occurrence.substr(0, end->occurrence.find('/'));
      const auto found = occurrences.find(first);
      if (found == occurrences.end()) {
        fail("unknown_id", q + "/occurrence", "unknown occurrence \"" + first + "\"");
      } else if (end->occurrence.find('/') != std::string::npos && scope_of(first)) {
        fail("schema", q + "/occurrence",
             "only occurrences of referenced components contain nested occurrences");
      }
      placement(end->offset, q + "/offset");
      const auto* scope = scope_of(end->occurrence);
      if (!scope) continue;
      const auto prefix = q + "/frame";
      for (const auto& link : assembly) {
        if (link.definition || !link.pointer.starts_with(prefix + "/")) continue;
        const auto& names = link.kind == "feature" ? scope->features
                            : link.kind == "body"  ? scope->bodies
                                                   : scope->entities;
        if (link.kind != "feature" && link.kind != "body" && link.kind != "entity") continue;
        if (!names.contains(link.target))
          fail("unknown_id", link.pointer,
               "unknown ID \"" + link.target + "\" in occurrence \"" + first + "\"");
      }
    }
    if (joint.a.occurrence == joint.b.occurrence)
      fail("schema", p + "/b/occurrence", "a joint joins two different occurrences");

    const auto dimensions = freedoms(joint.kind);
    if (!dimensions) continue;  // the schema reports the kind
    const auto n = dimensions->size();
    const bool driven = joint.kind != "rigid" && joint.kind != "ball";
    const auto count = [&](std::size_t size) {
      return "a " + joint.kind + " joint takes " + std::to_string(n) +
             (n == 1 ? " entry" : " entries") + ", not " + std::to_string(size);
    };
    if (joint.value) {
      const auto& v = *joint.value;
      if (!driven) {
        fail("schema", p + "/value", "a " + joint.kind + " joint takes no value");
      } else if (!v.if_array()) {
        if (n != 1)
          fail("schema", p + "/value", "a single value drives one degree of freedom; " + count(1));
        else
          quantity(v, (*dimensions)[0], p + "/value");
      } else if (v.if_array()->size() != n) {
        fail("schema", p + "/value", count(v.if_array()->size()));
      } else {
        for (std::size_t k = 0; k < n; ++k)
          if (!(*v.if_array())[k].is_null())
            quantity((*v.if_array())[k], (*dimensions)[k], p + "/value/" + std::to_string(k));
      }
    }
    if (joint.limits) {
      const auto& l = *joint.limits;
      const auto range = [&](const Value& r, Dimension dimension, const std::string& q) {
        if (!r.if_array() || r.if_array()->size() != 2) return;
        const auto low = quantity((*r.if_array())[0], dimension, q + "/0");
        const auto high = quantity((*r.if_array())[1], dimension, q + "/1");
        if (low && high && *low > *high) fail("bounds", q, "the low limit exceeds the high limit");
      };
      const bool single = l.if_array() && l.if_array()->size() == 2 &&
                          plain_value((*l.if_array())[0]) && plain_value((*l.if_array())[1]);
      if (!driven) {
        fail("schema", p + "/limits", "a " + joint.kind + " joint takes no limits");
      } else if (single) {
        if (n != 1)
          fail("schema", p + "/limits", "a single range limits one degree of freedom; " + count(1));
        else
          range(l, (*dimensions)[0], p + "/limits");
      } else if (!l.if_array() || l.if_array()->size() != n) {
        fail("schema", p + "/limits", count(l.if_array() ? l.if_array()->size() : 0));
      } else {
        for (std::size_t k = 0; k < n; ++k)
          if (!(*l.if_array())[k].is_null())
            range((*l.if_array())[k], (*dimensions)[k], p + "/limits/" + std::to_string(k));
      }
    }
  }
  return errors;
}
}  // namespace nf::ir::detail
