// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "nf/ir/document.hpp"
#include "nf/ir/migration.hpp"
#include "nf/ir/parameters.hpp"
#include "schema.hpp"

#include <algorithm>
#include <cmath>
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
  void optional_array(const Value& root, std::string_view key, F each) {
    if (root.find(key)) array(root, key, each);
  }
  /// Parameter overrides: an object of parameter IDs (or x- keys) to values.
  Object overrides(const Value& v, const std::string& p) {
    const auto* f = v.find("params");
    if (!f) return {};
    const auto pointer = at(p, "params");
    if (!f->if_object()) {
      fail(pointer, "expected an object of parameter overrides");
      return {};
    }
    Object result;
    for (const auto& m : *f->if_object()) {
      if (!m.key.starts_with("x-") && !identifier(m.key))
        fail(at(pointer, m.key), "parameter ID must match [a-z_][a-z0-9_]{0,63}");
      result.push_back(
          {m.key, m.key.starts_with("x-") ? m.value : numeric(&m.value, at(pointer, m.key))});
    }
    return result;
  }
  /// RFC 0013 density in kg/m^3: a finite positive number.
  std::optional<double> optional_density(const Value& v, const std::string& p) {
    const auto* f = v.find("density");
    if (!f) return std::nullopt;
    if (!f->if_number() || !std::isfinite(*f->if_number()) || !(*f->if_number() > 0)) {
      fail(at(p, "density"), "expected a positive density in kg/m^3");
      return std::nullopt;
    }
    return *f->if_number();
  }
  std::optional<bool> optional_bool(const Value& v, std::string_view key, const std::string& p) {
    const auto* f = v.find(key);
    if (!f) return std::nullopt;
    if (!f->if_bool()) {
      fail(at(p, key), "expected a boolean");
      return std::nullopt;
    }
    return *f->if_bool();
  }
  std::optional<Feature> feature(const Value& v, const std::string& p,
                                 std::unordered_set<std::string>& ids) {
    Feature feature;
    if (!object(&v, p, {"id", "type", "name", "suppressed", "inputs"}, feature.extensions))
      return std::nullopt;
    feature.id = id(v.find("id"), at(p, "id"), ids);
    feature.type = string(v.find("type"), at(p, "type"));
    if (!one_of(feature.type, {"sketch", "extrude", "revolve", "sweep", "loft", "fillet", "chamfer",
                               "shell", "hole", "boolean", "pattern_linear", "pattern_circular",
                               "mirror", "transform", "import", "script"}))
      fail(at(p, "type"), "unknown feature type");
    feature.name = optional_string(v, "name", p);
    feature.suppressed = optional_bool(v, "suppressed", p);
    if (const auto* f = v.find("inputs"); f && f->if_object())
      feature.inputs = *f;
    else
      fail(at(p, "inputs"), "expected an object");
    return feature;
  }
  JointEnd joint_end(const Value* v, const std::string& p) {
    JointEnd end;
    if (!object(v, p, {"occurrence", "frame", "rule", "offset", "flip"}, end.extensions))
      return end;
    end.occurrence = string(v->find("occurrence"), at(p, "occurrence"), true);
    if (const auto* f = v->find("frame"); f && f->if_object())
      end.frame = *f;
    else
      fail(at(p, "frame"), "expected a selector object");
    end.rule = optional_string(*v, "rule", p);
    if (const auto* f = v->find("offset")) end.offset = *f;
    end.flip = optional_bool(*v, "flip", p);
    return end;
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

std::expected<Document, Errors> decode(const Value& authored) {
  Reader r;
  if (!authored.if_object()) {
    r.fail("", "expected a part document object");
    return std::unexpected(std::move(r.errors));
  }
  if (r.string(authored.find("format"), "/format") != "nightfall.part")
    r.fail("/format", "expected format nightfall.part");
  const auto* version = authored.find("version");
  const auto n = version ? version->if_integer() : std::nullopt;
  if (!n || *n < 1 || *n > kDocumentVersion)
    r.fail("/version", n && *n > kDocumentVersion
                           ? "made by a newer Nightfall (version " + std::to_string(*n) + ")"
                           : "unsupported version; expected 1 or 2");
  if (!r.errors.empty()) return std::unexpected(std::move(r.errors));
  // Older files migrate forward first (RFC 0005); assembly sections exist only from version 2.
  const Value root = migrate(authored);
  const bool assembly = *n >= 2;

  Document d;
  r.object(&root, "",
           assembly
               ? Names{"format", "version", "units", "meta", "params", "references", "features",
                       "components", "occurrences", "joints", "studies", "history_end", "ui"}
               : Names{"format", "version", "units", "meta", "params", "references", "features",
                       "history_end", "ui"},
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
    if (auto feature = r.feature(v, p, ids)) d.features.push_back(std::move(*feature));
  });
  if (assembly) {
    r.optional_array(root, "components", [&](const Value& v, const std::string& p, auto& ids) {
      Component c;
      if (!r.object(&v, p, {"id", "features", "reference", "params", "density"}, c.extensions))
        return;
      c.id = r.id(v.find("id"), at(p, "id"), ids);
      if (c.id == "root") r.fail(at(p, "id"), "\"root\" names the document's own features");
      if (const auto* f = v.find("features")) {
        if (!f->if_array()) {
          r.fail(at(p, "features"), "expected an array");
        } else {
          std::vector<Feature> features;
          std::unordered_set<std::string> feature_ids;
          for (std::size_t i = 0; i < f->if_array()->size(); ++i)
            if (auto feature = r.feature((*f->if_array())[i],
                                         at(at(p, "features"), std::to_string(i)), feature_ids))
              features.push_back(std::move(*feature));
          c.features = std::move(features);
        }
      }
      c.reference = r.optional_string(v, "reference", p);
      if (v.find("features") && v.find("reference"))
        r.fail(p, "a component has inline features or a reference, not both");
      else if (!v.find("features") && !v.find("reference"))
        r.fail(p, "a component needs inline features or a reference");
      c.params = r.overrides(v, p);
      c.density = r.optional_density(v, p);
      d.components.push_back(std::move(c));
    });
    r.optional_array(root, "occurrences", [&](const Value& v, const std::string& p, auto& ids) {
      Occurrence o;
      if (!r.object(&v, p, {"id", "component", "params", "grounded", "placement", "density"},
                    o.extensions))
        return;
      o.id = r.id(v.find("id"), at(p, "id"), ids);
      o.component = r.string(v.find("component"), at(p, "component"), true);
      o.params = r.overrides(v, p);
      o.grounded = r.optional_bool(v, "grounded", p);
      if (const auto* f = v.find("placement")) o.placement = *f;
      o.density = r.optional_density(v, p);
      d.occurrences.push_back(std::move(o));
    });
    r.optional_array(root, "studies", [&](const Value& v, const std::string& p, auto& ids) {
      if (const auto* id = v.find("id"); id && id->if_string()) (void)r.id(id, at(p, "id"), ids);
      d.studies.push_back(v);
    });
    r.optional_array(root, "joints", [&](const Value& v, const std::string& p, auto& ids) {
      Joint j;
      if (!r.object(&v, p, {"id", "kind", "a", "b", "value", "limits"}, j.extensions)) return;
      j.id = r.id(v.find("id"), at(p, "id"), ids);
      j.kind = r.string(v.find("kind"), at(p, "kind"));
      j.a = r.joint_end(v.find("a"), at(p, "a"));
      j.b = r.joint_end(v.find("b"), at(p, "b"));
      if (const auto* f = v.find("value")) j.value = *f;
      if (const auto* f = v.find("limits")) j.limits = *f;
      d.joints.push_back(std::move(j));
    });
  }
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
  // Root feature links resolve in the root scope; each inline component's in its own scope.
  std::vector<detail::Link> root_links, assembly_links;
  std::vector<std::vector<detail::Link>> component_links(d.components.size());
  for (auto& link : schema.links) {
    if (link.pointer.starts_with("/features/")) {
      root_links.push_back(link);
      continue;
    }
    if (link.pointer.starts_with("/components/")) {
      const auto slash = link.pointer.find('/', 12);
      const auto k = std::stoul(link.pointer.substr(12, slash - 12));
      const auto rest = link.pointer.substr(slash);
      if (rest.starts_with("/features/") && k < component_links.size()) {
        auto local = link;
        local.pointer = rest;
        component_links[k].push_back(std::move(local));
        continue;
      }
    }
    // A study's selectors name root features of the body it runs on; the runner resolves them.
    if (link.pointer.starts_with("/studies/")) continue;
    assembly_links.push_back(std::move(link));
  }
  Errors references = detail::check_links(d, root_links);
  for (std::size_t k = 0; k < d.components.size(); ++k) {
    if (!d.components[k].features) continue;
    Document scope;
    scope.features = *d.components[k].features;
    scope.references = d.references;
    for (auto error : detail::check_links(scope, component_links[k])) {
      error.pointer = "/components/" + std::to_string(k) + error.pointer;
      if (references.size() < 128) references.push_back(std::move(error));
    }
  }
  if (!references.empty()) return std::unexpected(std::move(references));
  auto parameters = evaluate_parameters(d.params);
  if (!parameters) return std::unexpected(std::move(parameters.error()));
  auto assembled = detail::check_assembly(d, schema.links, assembly_links, parameters->values);
  if (!assembled.empty()) return std::unexpected(std::move(assembled));
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

Value encode_feature(const Feature& f) {
  Object o{{"id", f.id}, {"type", f.type}};
  optional(o, "name", f.name);
  optional(o, "suppressed", f.suppressed);
  o.push_back({"inputs", f.inputs});
  return extended(std::move(o), f.extensions);
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
  for (const auto& f : d.features) features.push_back(encode_feature(f));
  Object root{{"format", "nightfall.part"},
              {"version", Value(double(kDocumentVersion))},
              {"units", extended({{"length", d.units.length}, {"angle", d.units.angle}},
                                 d.units.extensions)},
              {"meta", extended(std::move(meta), d.meta.extensions)},
              {"params", std::move(params)},
              {"references", std::move(references)},
              {"features", std::move(features)}};
  // RFC 0013 sections, in RFC order, only when present: plain parts stay unchanged.
  if (!d.components.empty()) {
    Array components;
    for (const auto& c : d.components) {
      Object o{{"id", c.id}};
      if (c.features) {
        Array list;
        for (const auto& f : *c.features) list.push_back(encode_feature(f));
        o.push_back({"features", std::move(list)});
      }
      optional(o, "reference", c.reference);
      if (!c.params.empty()) o.push_back({"params", c.params});
      optional(o, "density", c.density);
      components.push_back(extended(std::move(o), c.extensions));
    }
    root.push_back({"components", std::move(components)});
  }
  if (!d.occurrences.empty()) {
    Array occurrences;
    for (const auto& o : d.occurrences) {
      Object e{{"id", o.id}, {"component", o.component}};
      if (!o.params.empty()) e.push_back({"params", o.params});
      optional(e, "grounded", o.grounded);
      optional(e, "placement", o.placement);
      optional(e, "density", o.density);
      occurrences.push_back(extended(std::move(e), o.extensions));
    }
    root.push_back({"occurrences", std::move(occurrences)});
  }
  if (!d.joints.empty()) {
    const auto end = [](const JointEnd& e) {
      Object o{{"occurrence", e.occurrence}, {"frame", e.frame}};
      optional(o, "rule", e.rule);
      optional(o, "offset", e.offset);
      optional(o, "flip", e.flip);
      return extended(std::move(o), e.extensions);
    };
    Array joints;
    for (const auto& j : d.joints) {
      Object o{{"id", j.id}, {"kind", j.kind}, {"a", end(j.a)}, {"b", end(j.b)}};
      optional(o, "value", j.value);
      optional(o, "limits", j.limits);
      joints.push_back(extended(std::move(o), j.extensions));
    }
    root.push_back({"joints", std::move(joints)});
  }
  if (!d.studies.empty()) root.push_back({"studies", Array(d.studies.begin(), d.studies.end())});
  root.push_back({"history_end", d.history_end ? Value(*d.history_end) : Value()});
  root.push_back({"ui", d.ui});
  return extended(std::move(root), d.extensions);
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
  for (std::size_t k = 0; k < document.components.size(); ++k) {
    const auto& c = document.components[k];
    const auto p = at("/components", std::to_string(k));
    extensions(c.extensions, p, 3);
    for (const auto& m : c.params) check(m.value, at(at(p, "params"), m.key), 4);
    if (c.features)
      for (std::size_t i = 0; i < c.features->size(); ++i) {
        const auto f = at(at(p, "features"), std::to_string(i));
        extensions((*c.features)[i].extensions, f, 5);
        check((*c.features)[i].inputs, at(f, "inputs"), 5);
      }
  }
  for (std::size_t k = 0; k < document.occurrences.size(); ++k) {
    const auto& o = document.occurrences[k];
    const auto p = at("/occurrences", std::to_string(k));
    extensions(o.extensions, p, 3);
    for (const auto& m : o.params) check(m.value, at(at(p, "params"), m.key), 4);
    if (o.placement) check(*o.placement, at(p, "placement"), 3);
  }
  for (std::size_t k = 0; k < document.joints.size(); ++k) {
    const auto& j = document.joints[k];
    const auto p = at("/joints", std::to_string(k));
    extensions(j.extensions, p, 3);
    for (const auto* e : {&j.a, &j.b}) {
      const auto q = at(p, e == &j.a ? "a" : "b");
      extensions(e->extensions, q, 4);
      check(e->frame, at(q, "frame"), 4);
      if (e->offset) check(*e->offset, at(q, "offset"), 4);
    }
    if (j.value) check(*j.value, at(p, "value"), 3);
    if (j.limits) check(*j.limits, at(p, "limits"), 3);
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
