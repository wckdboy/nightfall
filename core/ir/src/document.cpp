// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "nf/ir/document.hpp"
#include "schema.hpp"

#include <algorithm>
#include <unordered_set>

namespace nf::ir {
namespace {

using json::Array;
using json::Object;
using json::Value;
using Names = std::initializer_list<std::string_view>;

bool one_of(std::string_view name, Names names) {
  return std::ranges::find(names, name) != names.end();
}

std::string at(std::string_view parent, std::string_view key) {
  std::string p(parent);
  p += '/';
  for (const char c : key) p += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
  return p;
}

bool identifier(std::string_view s) {
  const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || c == '_'; };
  return !s.empty() && s.size() <= 64 && letter(s.front()) &&
         std::ranges::all_of(s, [&](char c) { return letter(c) || (c >= '0' && c <= '9'); });
}

class Reader {
 public:
  Errors errors;
  void fail(std::string pointer, std::string message, std::string code = "schema") {
    if (errors.size() < 128)
      errors.push_back({std::move(code), std::move(message), std::move(pointer), std::nullopt});
  }
  bool object(const Value* v, const std::string& p, Names keys, Object& extensions) {
    if (!v || !v->if_object()) {
      fail(p, "expected an object");
      return false;
    }
    for (const auto& m : *v->if_object()) {
      if (m.key.starts_with("x-"))
        extensions.push_back(m);
      else if (!one_of(m.key, keys))
        fail(at(p, m.key), "unknown field");
    }
    return true;
  }
  std::string string(const Value* v, const std::string& p, bool nonempty = false) {
    if (v && v->if_string() && (!nonempty || !v->if_string()->empty())) return *v->if_string();
    fail(p, nonempty ? "expected a nonempty string" : "expected a string");
    return {};
  }
  std::optional<std::string> optional_string(const Value& v, std::string_view key,
                                             const std::string& p) {
    if (const auto* f = v.find(key)) return string(f, at(p, key));
    return std::nullopt;
  }
  Value numeric(const Value* v, const std::string& p) {
    if (v && (v->if_number() || v->if_string())) return *v;
    fail(p, "expected a number or expression string");
    return {};
  }
  std::string id(const Value* v, const std::string& p, std::unordered_set<std::string>& ids) {
    const auto s = string(v, p);
    if (!identifier(s))
      fail(p, "ID must match [a-z_][a-z0-9_]{0,63}");
    else if (!ids.insert(s).second)
      fail(p, "duplicate ID \"" + s + "\"");
    return s;
  }
  template <class F>
  void array(const Value& root, std::string_view key, F each) {
    const auto* v = root.find(key);
    const std::string p = at("", key);
    if (!v || !v->if_array()) {
      fail(p, "expected an array");
      return;
    }
    std::unordered_set<std::string> ids;
    for (std::size_t i = 0; i < v->if_array()->size(); ++i)
      each((*v->if_array())[i], at(p, std::to_string(i)), ids);
  }
};

std::expected<Document, Errors> decode(const Value& root) {
  Reader r;
  if (!root.if_object()) {
    r.fail("", "expected a part document object");
    return std::unexpected(std::move(r.errors));
  }
  if (r.string(root.find("format"), "/format") != "nightfall.part")
    r.fail("/format", "expected format nightfall.part");
  const auto* version = root.find("version");
  const auto n = version ? version->if_integer() : std::nullopt;
  if (!n || *n != kDocumentVersion)
    r.fail("/version", n && *n > kDocumentVersion
                           ? "made by a newer Nightfall (version " + std::to_string(*n) + ")"
                           : "unsupported version; expected 1");
  if (!r.errors.empty()) return std::unexpected(std::move(r.errors));

  Document d;
  r.object(&root, "",
           {"format", "version", "units", "meta", "params", "references", "features", "history_end",
            "ui"},
           d.extensions);
  if (const auto* v = root.find("units");
      r.object(v, "/units", {"length", "angle"}, d.units.extensions)) {
    d.units.length = r.string(v->find("length"), "/units/length");
    d.units.angle = r.string(v->find("angle"), "/units/angle");
    if (!one_of(d.units.length, {"mm", "cm", "m", "in", "ft"}))
      r.fail("/units/length", "expected mm, cm, m, in or ft");
    if (!one_of(d.units.angle, {"deg", "rad"})) r.fail("/units/angle", "expected deg or rad");
  }
  if (const auto* v = root.find("meta"); r.object(v, "/meta", {"name", "doc"}, d.meta.extensions)) {
    d.meta.name = r.optional_string(*v, "name", "/meta");
    d.meta.doc = r.optional_string(*v, "doc", "/meta");
  }
  r.array(root, "params", [&](const Value& v, const std::string& p, auto& ids) {
    Parameter param;
    if (!r.object(&v, p, {"name", "value", "min", "max", "doc"}, param.extensions)) return;
    param.name = r.id(v.find("name"), at(p, "name"), ids);
    param.value = r.numeric(v.find("value"), at(p, "value"));
    if (const auto* f = v.find("min")) param.min = r.numeric(f, at(p, "min"));
    if (const auto* f = v.find("max")) param.max = r.numeric(f, at(p, "max"));
    param.doc = r.optional_string(v, "doc", p);
    d.params.push_back(std::move(param));
  });
  r.array(root, "references", [&](const Value& v, const std::string& p, auto& ids) {
    Reference ref;
    if (!r.object(&v, p, {"id", "kind", "path", "sha256"}, ref.extensions)) return;
    ref.id = r.id(v.find("id"), at(p, "id"), ids);
    ref.kind = r.string(v.find("kind"), at(p, "kind"), true);
    ref.path = r.string(v.find("path"), at(p, "path"), true);
    ref.sha256 = r.string(v.find("sha256"), at(p, "sha256"));
    if (ref.sha256.size() != 64 || !std::ranges::all_of(ref.sha256, [](char c) {
          return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        }))
      r.fail(at(p, "sha256"), "expected a 64-digit hexadecimal SHA-256 digest");
    d.references.push_back(std::move(ref));
  });
  r.array(root, "features", [&](const Value& v, const std::string& p, auto& ids) {
    Feature feature;
    if (!r.object(&v, p, {"id", "type", "name", "suppressed", "inputs"}, feature.extensions))
      return;
    feature.id = r.id(v.find("id"), at(p, "id"), ids);
    feature.type = r.string(v.find("type"), at(p, "type"));
    if (!one_of(feature.type, {"sketch", "extrude", "revolve", "sweep", "loft", "fillet", "chamfer",
                               "shell", "hole", "boolean", "pattern_linear", "pattern_circular",
                               "mirror", "transform", "import", "script"}))
      r.fail(at(p, "type"), "unknown feature type");
    feature.name = r.optional_string(v, "name", p);
    if (const auto* f = v.find("suppressed")) {
      if (f->if_bool())
        feature.suppressed = *f->if_bool();
      else
        r.fail(at(p, "suppressed"), "expected a boolean");
    }
    if (const auto* f = v.find("inputs"); f && f->if_object())
      feature.inputs = *f;
    else
      r.fail(at(p, "inputs"), "expected an object");
    d.features.push_back(std::move(feature));
  });
  const auto* end = root.find("history_end");
  if (!end || !end->is_null()) {
    d.history_end = r.string(end, "/history_end");
    if (!identifier(*d.history_end))
      r.fail("/history_end", "expected a feature ID or null");
    else if (std::ranges::none_of(d.features, [&](const auto& f) { return f.id == d.history_end; }))
      r.fail("/history_end", "unknown feature \"" + *d.history_end + "\"", "unknown_id");
  }
  if (const auto* ui = root.find("ui"); ui && ui->if_object())
    d.ui = *ui->if_object();
  else
    r.fail("/ui", "expected an object");
  if (!r.errors.empty()) return std::unexpected(std::move(r.errors));
  auto schema = detail::check_schema(root);
  if (!schema.errors.empty()) return std::unexpected(std::move(schema.errors));
  auto references = detail::check_links(d, schema.links);
  if (!references.empty()) return std::unexpected(std::move(references));
  return d;
}

template <class T>
void optional(Object& o, std::string key, const std::optional<T>& value) {
  if (value) o.push_back({std::move(key), Value(*value)});
}
Value extended(Object o, const Object& extensions) {
  o.insert(o.end(), extensions.begin(), extensions.end());
  return Value(std::move(o));
}

Value encode(const Document& d) {
  Object meta;
  optional(meta, "name", d.meta.name);
  optional(meta, "doc", d.meta.doc);
  Array params, references, features;
  for (const auto& p : d.params) {
    Object o{{"name", p.name}, {"value", p.value}};
    optional(o, "min", p.min);
    optional(o, "max", p.max);
    optional(o, "doc", p.doc);
    params.push_back(extended(std::move(o), p.extensions));
  }
  for (const auto& ref : d.references)
    references.push_back(
        extended({{"id", ref.id}, {"kind", ref.kind}, {"path", ref.path}, {"sha256", ref.sha256}},
                 ref.extensions));
  for (const auto& f : d.features) {
    Object o{{"id", f.id}, {"type", f.type}};
    optional(o, "name", f.name);
    optional(o, "suppressed", f.suppressed);
    o.push_back({"inputs", f.inputs});
    features.push_back(extended(std::move(o), f.extensions));
  }
  return extended({{"format", "nightfall.part"},
                   {"version", Value(double(kDocumentVersion))},
                   {"units", extended({{"length", d.units.length}, {"angle", d.units.angle}},
                                      d.units.extensions)},
                   {"meta", extended(std::move(meta), d.meta.extensions)},
                   {"params", std::move(params)},
                   {"references", std::move(references)},
                   {"features", std::move(features)},
                   {"history_end", d.history_end ? Value(*d.history_end) : Value()},
                   {"ui", d.ui}},
                  d.extensions);
}

bool bounded(const Value& v, int depth = 0) {
  if (v.if_array() || v.if_object()) {
    if (depth >= json::kMaxDepth) return false;
    if (v.if_array()) {
      for (const auto& item : *v.if_array())
        if (!bounded(item, depth + 1)) return false;
    } else {
      for (const auto& m : *v.if_object())
        if (!bounded(m.value, depth + 1)) return false;
    }
  }
  return true;
}

}  // namespace

std::expected<Document, Errors> read_document(std::string_view text) {
  if (text.size() > kMaxDocumentBytes)
    return std::unexpected(
        Errors{{"parse", "document exceeds the 16 MiB size limit", "", std::nullopt}});
  auto value = json::parse(text);
  if (!value)
    return std::unexpected(Errors{{"parse", value.error().message, "", value.error().offset}});
  return decode(*value);
}

std::expected<std::string, Errors> write_document(const Document& document) {
  Reader r;
  const auto check = [&](const Value& v, const std::string& p, int depth) {
    if (!bounded(v, depth)) r.fail(p, "nesting is too deep", "parse");
  };
  const auto extensions = [&](const Object& o, const std::string& p, int depth) {
    for (const auto& m : o) {
      if (!m.key.starts_with("x-")) r.fail(at(p, m.key), "extension keys must start with x-");
      check(m.value, at(p, m.key), depth);
    }
  };
  extensions(document.extensions, "", 1);
  extensions(document.units.extensions, "/units", 2);
  extensions(document.meta.extensions, "/meta", 2);
  for (std::size_t i = 0; i < document.params.size(); ++i) {
    const auto& param = document.params[i];
    const auto p = at("/params", std::to_string(i));
    extensions(param.extensions, p, 3);
    check(param.value, at(p, "value"), 3);
    if (param.min) check(*param.min, at(p, "min"), 3);
    if (param.max) check(*param.max, at(p, "max"), 3);
  }
  for (std::size_t i = 0; i < document.references.size(); ++i)
    extensions(document.references[i].extensions, at("/references", std::to_string(i)), 3);
  for (std::size_t i = 0; i < document.features.size(); ++i) {
    const auto p = at("/features", std::to_string(i));
    extensions(document.features[i].extensions, p, 3);
    check(document.features[i].inputs, at(p, "inputs"), 3);
  }
  for (const auto& m : document.ui) check(m.value, at("/ui", m.key), 2);
  if (!r.errors.empty()) return std::unexpected(std::move(r.errors));
  auto text = json::write(encode(document));
  // The reader also catches invalid UTF-8, duplicate extension keys and invalid edits.
  if (auto checked = read_document(text); !checked)
    return std::unexpected(std::move(checked.error()));
  return text;
}

}  // namespace nf::ir
