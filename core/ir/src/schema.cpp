// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "schema.hpp"
#include "schema_data.hpp"

#include <algorithm>
#include <regex>
#include <unordered_map>

namespace nf::ir {
std::string_view part_schema() noexcept { return detail::kPartSchema; }
}  // namespace nf::ir

namespace nf::ir::detail {
namespace {
using json::Value;
std::string child(std::string p, std::string_view key) {
  p += '/';
  for (char c : key) p += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
  return p;
}

struct Catalog {
  Value root = json::parse(kPartSchema).value();
  std::unordered_map<std::string, const Value*> definitions;
  std::unordered_map<std::string, std::regex> patterns;
  Catalog() {
    for (const auto& m : *root.find("$defs")->if_object())
      definitions.emplace("#/$defs/" + m.key, &m.value);
    compile(root);
  }
  const Value& resolved(const Value& schema) const {
    const Value* s = &schema;
    std::size_t hops = 0;
    while (const auto* ref = s->find("$ref")) {
      if (++hops > definitions.size()) throw std::logic_error("cyclic schema alias");
      s = definitions.at(*ref->if_string());
    }
    return *s;
  }
  bool matches(const std::string& text, const std::string& pattern) const {
    return std::regex_search(text, patterns.at(pattern));
  }
  void compile(const Value& s) {
    if (s.if_bool()) return;
    for (const auto& m : *s.if_object()) {
      const auto& k = m.key;
      const auto& v = m.value;
      if (k == "$defs" || k == "properties" || k == "patternProperties") {
        for (const auto& entry : *v.if_object()) {
          if (k == "patternProperties") patterns.try_emplace(entry.key, entry.key);
          compile(entry.value);
        }
      } else if (k == "anyOf" || k == "oneOf" || k == "allOf") {
        for (const auto& item : *v.if_array()) compile(item);
      } else if (k == "items" || k == "additionalProperties" || k == "not") {
        compile(v);
      } else if (k == "pattern") {
        patterns.try_emplace(*v.if_string(), *v.if_string());
      } else if (k == "x-nf-value") {
        if (!v.if_string() ||
            (*v.if_string() != "length" && *v.if_string() != "angle" &&
             *v.if_string() != "scalar" && *v.if_string() != "count" && *v.if_string() != "degree"))
          throw std::logic_error("unsupported schema field dimension");
      } else if (k == "type") {
        if (!v.if_string()) throw std::logic_error("schema type arrays are unsupported");
      } else if (k == "const" || k == "enum") {
        const auto scalar = [](const Value& value) {
          if (value.if_array() || value.if_object())
            throw std::logic_error("schema constants must be scalar");
        };
        if (k == "const")
          scalar(v);
        else
          for (const auto& value : *v.if_array()) scalar(value);
      } else if (k == "$ref") {
        (void)resolved(s);
        for (const auto& sibling : *s.if_object())
          if (sibling.key != "$ref" && !sibling.key.starts_with("x-nf-"))
            throw std::logic_error("unsupported schema reference sibling: " + sibling.key);
      } else if (k != "$schema" && k != "$id" && k != "$comment" && k != "title" && k != "type" &&
                 k != "enum" && k != "const" && k != "required" && k != "minimum" &&
                 k != "maximum" && k != "minItems" && k != "maxItems" && k != "minLength" &&
                 k != "x-nf-reference" && k != "x-nf-definition" && k != "x-nf-value") {
        throw std::logic_error("unsupported schema keyword: " + k);
      }
    }
  }
};

bool type_is(const Value& v, std::string_view type) {
  if (type == "object") return v.if_object();
  if (type == "array") return v.if_array();
  if (type == "string") return v.if_string();
  if (type == "number") return v.if_number();
  if (type == "integer") return v.if_number() && std::trunc(*v.if_number()) == *v.if_number();
  if (type == "boolean") return v.if_bool();
  if (type == "null") return v.is_null();
  throw std::logic_error("unsupported schema type");
}

class Checker {
 public:
  Checker(const Catalog& catalog, std::size_t& budget) : catalog_(catalog), budget_(budget) {}
  SchemaResult result;
  void fail(const std::string& p, std::string message) {
    if (result.errors.size() < 128)
      result.errors.push_back({"schema", std::move(message), p, std::nullopt});
  }
  void record(const Value& v, const Value& s, const std::string& p) {
    if (const auto* kind = s.find("x-nf-value"))
      result.fields.push_back({*kind->if_string(), p, &v});
    for (const auto* key : {"x-nf-reference", "x-nf-definition"}) {
      if (const auto* kind = s.find(key)) {
        std::string target;
        if (v.if_string())
          target = *v.if_string();
        else if (v.find("sketch") && v.find("entity") && v.find("sketch")->if_string() &&
                 v.find("entity")->if_string())
          target = *v.find("sketch")->if_string() + "/" + *v.find("entity")->if_string();
        if (!target.empty())
          result.links.push_back({*kind->if_string(), std::move(target), p,
                                  std::string_view(key) == "x-nf-definition"});
      }
    }
  }
  void check(const Value& v, const Value& schema, const std::string& p, int depth = 0) {
    if (budget_ == 0 || depth > json::kMaxDepth) {
      fail(p, "validation work/depth limit exceeded");
      return;
    }
    --budget_;
    const Value* rule = &schema;
    while (const auto* ref = rule->find("$ref")) {
      record(v, *rule, p);
      rule = catalog_.definitions.at(*ref->if_string());
    }
    const Value& s = *rule;
    if (s.if_bool()) {
      if (!*s.if_bool()) fail(p, "value is not allowed");
      return;
    }
    if (const auto* t = s.find("type"); t && !type_is(v, *t->if_string())) {
      fail(p, "expected " + *t->if_string());
      return;
    }
    record(v, s, p);
    if (const auto* c = s.find("const"); c && v != *c) fail(p, "unexpected constant value");
    if (const auto* e = s.find("enum");
        e && std::ranges::find(*e->if_array(), v) == e->if_array()->end())
      fail(p, "unknown option");
    if (v.if_number()) {
      for (const auto* key : {"minimum", "maximum"})
        if (const auto* bound = s.find(key);
            bound && (std::string_view(key) == "minimum" ? *v.if_number() < *bound->if_number()
                                                         : *v.if_number() > *bound->if_number()))
          fail(p, "number outside allowed range");
    }
    if (const auto* str = v.if_string()) {
      if (const auto* pattern = s.find("pattern");
          pattern && !catalog_.matches(*str, *pattern->if_string()))
        fail(p, "invalid string format");
      const auto length = std::ranges::count_if(
          *str, [](char c) { return (static_cast<unsigned char>(c) & 0xc0) != 0x80; });
      if (const auto* min = s.find("minLength");
          min && static_cast<double>(length) < *min->if_number())
        fail(p, "string is too short");
    }
    if (const auto* a = v.if_array()) {
      if (const auto* min = s.find("minItems");
          min && static_cast<double>(a->size()) < *min->if_number())
        fail(p, "too few array elements");
      if (const auto* max = s.find("maxItems");
          max && static_cast<double>(a->size()) > *max->if_number())
        fail(p, "too many array elements");
      if (const auto* items = s.find("items"))
        for (std::size_t i = 0; i < a->size(); ++i)
          check((*a)[i], *items, child(p, std::to_string(i)), depth + 1);
    }
    if (v.if_object()) object(v, s, p, depth);
    for (const auto* kind : {"anyOf", "oneOf"})
      if (const auto* alternatives = s.find(kind))
        alternatives_check(v, *alternatives->if_array(), p, depth,
                           std::string_view(kind) == "oneOf");
    if (const auto* all = s.find("allOf"))
      for (const auto& part : *all->if_array()) check(v, part, p, depth);
    if (const auto* negated = s.find("not")) {
      Checker probe(catalog_, budget_);
      probe.check(v, *negated, p, depth);
      if (probe.result.errors.empty()) fail(p, "conflicting fields");
    }
  }

 private:
  void object(const Value& v, const Value& s, const std::string& p, int depth) {
    const auto before = result.errors.size();
    if (const auto* required = s.find("required"))
      for (const auto& key : *required->if_array())
        if (!v.find(*key.if_string()))
          fail(child(p, *key.if_string()), "required field is missing");
    const auto* props = s.find("properties");
    const auto* patterns = s.find("patternProperties");
    const auto* extra = s.find("additionalProperties");
    const auto matches = [&](const std::string& key) {
      if (props && props->find(key)) return true;
      if (patterns)
        for (const auto& pattern : *patterns->if_object())
          if (catalog_.matches(key, pattern.key)) return true;
      return false;
    };
    if (extra && extra->if_bool() && !*extra->if_bool())
      for (const auto& m : *v.if_object())
        if (!matches(m.key)) fail(child(p, m.key), "unknown field");
    // Reject wrong alternatives before descending through common recursive selector fields.
    if (result.errors.size() != before) return;
    for (const auto& m : *v.if_object()) {
      if (const auto* field = props ? props->find(m.key) : nullptr)
        check(m.value, *field, child(p, m.key), depth + 1);
      if (patterns)
        for (const auto& pattern : *patterns->if_object())
          if (catalog_.matches(m.key, pattern.key))
            check(m.value, pattern.value, child(p, m.key), depth + 1);
      if (extra && !matches(m.key)) check(m.value, *extra, child(p, m.key), depth + 1);
    }
  }
  void alternatives_check(const Value& v, const json::Array& choices, const std::string& p,
                          int depth, bool exactly_one) {
    // Disjoint type tags select features/entities/constraints without trying every variant.
    bool tagged = v.if_object();
    const Value* selected = nullptr;
    for (const auto& choice : choices) {
      const auto& resolved = catalog_.resolved(choice);
      const auto* props = resolved.find("properties");
      const auto* required = resolved.find("required");
      const auto* tag = props ? props->find("type") : nullptr;
      const auto* constant = tag ? tag->find("const") : nullptr;
      if (!constant || !required ||
          std::ranges::find(*required->if_array(), Value("type")) == required->if_array()->end()) {
        tagged = false;
        break;
      }
      if (v.find("type") && *v.find("type") == *constant) {
        if (selected) {
          tagged = false;
          break;
        }
        selected = &choice;
      }
    }
    if (tagged) {
      if (selected)
        check(v, *selected, p, depth);
      else
        fail(child(p, "type"), "unknown type");
      return;
    }
    int successes = 0;
    std::size_t best_score = 0;
    Errors best;
    std::vector<Link> links;
    std::vector<ValueField> fields;
    for (const auto& choice : choices) {
      Checker probe(catalog_, budget_);
      probe.check(v, choice, p, depth);
      if (probe.result.errors.empty()) {
        ++successes;
        links = std::move(probe.result.links);
        fields = std::move(probe.result.fields);
        if (!exactly_one) break;
      } else {
        std::size_t score = 0;
        if (const auto* props = catalog_.resolved(choice).find("properties"))
          for (const auto& field : *props->if_object())
            if (v.find(field.key)) ++score;
        if (best.empty() || score > best_score ||
            (score == best_score && probe.result.errors.size() < best.size())) {
          best_score = score;
          best = std::move(probe.result.errors);
        }
      }
    }
    if (successes == 0) {
      for (auto& error : best) fail(error.pointer, std::move(error.message));
      fail(p, "no matching input form");
    } else if (exactly_one && successes != 1)
      fail(p, "choose exactly one input form");
    else {
      result.links.insert(result.links.end(), std::make_move_iterator(links.begin()),
                          std::make_move_iterator(links.end()));
      result.fields.insert(result.fields.end(), std::make_move_iterator(fields.begin()),
                           std::make_move_iterator(fields.end()));
    }
  }
  const Catalog& catalog_;
  std::size_t& budget_;
};
const Catalog& catalog() {
  static const Catalog instance;
  return instance;
}
}  // namespace

SchemaResult check_schema(const Value& document, std::size_t work_limit) {
  Checker checker(catalog(), work_limit);
  checker.check(document, catalog().root, "");
  // Resource exhaustion must not become success through a negated or alternative schema.
  if (work_limit == 0) checker.fail("", "validation work limit exceeded");
  return std::move(checker.result);
}

SchemaResult check_feature(const Value& feature) {
  std::size_t budget = 1000000;
  Checker checker(catalog(), budget);
  checker.check(feature, *catalog().definitions.at("#/$defs/feature"), "");
  if (budget == 0) checker.fail("", "validation work limit exceeded");
  return std::move(checker.result);
}
}  // namespace nf::ir::detail
