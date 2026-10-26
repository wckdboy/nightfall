// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "session.hpp"

#include "assembly.hpp"
#include "assembly_queries.hpp"
#include "nf/ir/patch.hpp"

#include <unistd.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <numbers>

namespace nf::cli {
namespace {
namespace fs = std::filesystem;
using ir::json::Array;
using ir::json::Object;
using ir::json::Value;
using kernel_service::FeatureStatus;
Value numeric(std::size_t n) { return static_cast<double>(n); }
Value point(math::Vec3 p) { return Array{p.x, p.y, p.z}; }
[[noreturn]] void fail(std::string code, std::string message) {
  throw ir::Errors{{std::move(code), std::move(message), "", {}}};
}
[[noreturn]] void io_error() { fail("io", std::strerror(errno)); }
constexpr std::array<std::string_view, 5> kStatus{"built", "cached", "suppressed", "rolled_back",
                                                  "failed"};
bool live(const kernel_service::BodyState& body) { return body.shape && body.unavailable.empty(); }
Value optional_string(const std::optional<std::string>& s) {
  return s ? Value(*s) : Value(nullptr);
}
}  // namespace

std::string load(const fs::path& path) {
  if (!fs::is_regular_file(path)) fail("io", "input must be a regular file");
  std::ifstream stream(path, std::ios::binary);
  if (!stream) fail("io", "cannot open input");
  std::array<char, 8192> buffer;
  std::string text;
  while (stream) {
    stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = static_cast<std::size_t>(stream.gcount());
    if (count > ir::kMaxDocumentBytes - text.size())
      fail("parse", "document exceeds the 16 MiB limit");
    text.append(buffer.data(), count);
  }
  if (!stream.eof()) fail("io", "cannot read input");
  return text;
}

void publish(const fs::path& output, std::string_view text) {
  auto temporary = (output.parent_path() / ".nightfall-XXXXXX").string();
  int file = mkstemp(temporary.data());
  if (file < 0) io_error();
  try {
    while (!text.empty()) {
      const auto count = ::write(file, text.data(), text.size());
      if (count < 0 && errno == EINTR) continue;
      if (count < 0) io_error();
      if (count == 0) fail("io", "output write made no progress");
      text.remove_prefix(static_cast<std::size_t>(count));
    }
    if (fsync(file) != 0) io_error();
    const int closed = close(file);
    file = -1;
    if (closed != 0) io_error();
    if (std::rename(temporary.c_str(), output.c_str()) != 0) io_error();
  } catch (...) {
    if (file >= 0) close(file);
    std::remove(temporary.c_str());
    throw;
  }
}

std::expected<std::string, ir::Errors> apply_overrides(std::string source,
                                                       std::map<std::string, std::string> values) {
  if (values.empty()) return source;
  auto document = ir::read_document(source);
  if (!document) return std::unexpected(document.error());
  for (auto& parameter : document->params) {
    const auto found = values.find(parameter.name);
    if (found == values.end()) continue;
    parameter.value = found->second;
    values.erase(found);
  }
  if (!values.empty())
    return std::unexpected(ir::Errors{
        {"unknown_id", "unknown parameter override: " + values.begin()->first, "/params", {}}});
  // Validate the final graph and bounds once all definitions have been replaced.
  return ir::write_document(*document);
}

Object feature_counts(const kernel_service::Evaluation& evaluation,
                      const std::vector<kernel_service::FeatureStatus>& extra) {
  std::array<std::size_t, 5> counts{};
  for (const auto& f : evaluation.features) ++counts[static_cast<std::size_t>(f.status)];
  for (const auto status : extra) ++counts[static_cast<std::size_t>(status)];
  Object result;
  for (std::size_t i = 0; i < kStatus.size(); ++i)
    result.push_back({std::string(kStatus[i]), numeric(counts[i])});
  return result;
}

Object body_metrics(const std::string& id, const kernel_service::BodyState& body) {
  const auto& m = body.shape->measures();
  return {{"id", id},
          {"state_hash", body.hash->hex()},
          {"volume", m.volume},
          {"area", m.area},
          {"centroid", point(m.centroid)},
          {"minimum", point(m.minimum)},
          {"maximum", point(m.maximum)},
          {"solids", numeric(m.solids)},
          {"faces", numeric(m.faces)},
          {"edges", numeric(m.edges)},
          {"name_records", numeric(body.names.size())}};
}

ir::json::Array error_entries(const ir::Errors& errors) {
  Array entries;
  for (const auto& e : errors) {
    Object entry{{"code", e.code}, {"message", e.message}, {"pointer", e.pointer}};
    if (e.offset) entry.push_back({"offset", numeric(*e.offset)});
    entries.push_back(std::move(entry));
  }
  return entries;
}

void Session::adopt_invalid(std::string source, ir::Errors errors) {
  state_ = State{std::move(source), {}, {}, {}, std::move(errors), {}, {}};
  undo_.clear();
  redo_.clear();
}

Object Session::Build::json() const {
  Object result{{"ok", ok},
                {"evidence", evidence},
                {"features", features},
                {"bodies", bodies},
                {"changed_bodies", changed},
                {"added_bodies", added},
                {"removed_bodies", removed}};
  if (!written_back.empty()) result.push_back({"written_back", written_back});
  if (assembly) result.push_back({"assembly", *assembly});
  return result;
}

namespace {
/// RFC 0012 write-back: for every sketch the evaluator solved, the authored entities take the
/// solved coordinates wherever they were plain numbers, converted from millimetres and radians
/// to the document's units. A sketch with a coordinate authored as an expression is not
/// rewritten at all: writing its numbers alone would change the start the next solve begins
/// from, so the source would drift on every rebuild. Its expression entities are listed.
/// Returns the rewritten document text, or nothing when no sketch was solved or nothing could
/// change.
std::optional<std::string> solved_source(ir::Document document,
                                         const kernel_service::Evaluation& evaluation,
                                         Array& written) {
  static const std::map<std::string, double> mm_per{
      {"mm", 1.0}, {"cm", 10.0}, {"m", 1000.0}, {"in", 25.4}, {"ft", 304.8}};
  const double length =
      mm_per.count(document.units.length) ? mm_per.at(document.units.length) : 1.0;
  const double angle = document.units.angle == "deg" ? 180.0 / std::numbers::pi : 1.0;
  bool changed = false;
  for (std::size_t i = 0; i < document.features.size() && i < evaluation.features.size(); ++i) {
    const auto& report = evaluation.features[i].sketch;
    if (!report || !report->solved || !report->entities) continue;
    const auto* authored = document.features[i].inputs.find("entities");
    const auto* solved = report->entities->if_array();
    if (!authored || !authored->if_array() || !solved) continue;
    Array entities = *authored->if_array();
    Array updated, expressions;
    for (std::size_t k = 0; k < entities.size() && k < solved->size(); ++k) {
      ir::json::Object entity = *entities[k].if_object();
      const auto& result = (*solved)[k];
      bool touched = false, expression = false;
      for (auto& member : entity) {
        const auto* value = result.find(member.key);
        if (!value) continue;
        const bool is_angle = member.key == "start_angle" || member.key == "sweep_angle";
        const bool is_length = member.key == "at" || member.key == "start" || member.key == "end" ||
                               member.key == "center" || member.key == "radius";
        if (!is_angle && !is_length) continue;
        const double scale = is_angle ? angle : 1.0 / length;
        const auto convert = [&](const Value& authored_value, const Value& solved_value) -> Value {
          if (!authored_value.if_number()) {
            if (authored_value.if_string()) expression = true;
            return authored_value;
          }
          // Unit round trips (in -> mm -> in) are not exact: an unmoved number stays as written.
          const double authored = *authored_value.if_number();
          const double next = *solved_value.if_number() * scale;
          if (std::abs(next - authored) <= 1e-12 * std::max(1.0, std::abs(authored)))
            return authored_value;
          touched = true;
          return next;
        };
        if (const auto* pair = member.value.if_array()) {
          const auto* target = value->if_array();
          if (!target || target->size() != pair->size()) continue;
          Array next;
          for (std::size_t c = 0; c < pair->size(); ++c)
            next.push_back(convert((*pair)[c], (*target)[c]));
          member.value = std::move(next);
        } else if (value->if_number()) {
          member.value = convert(member.value, *value);
        }
      }
      if (touched) updated.push_back(*entities[k].find("id"));
      if (expression) expressions.push_back(*entities[k].find("id"));
      entities[k] = std::move(entity);
    }
    if (updated.empty() && expressions.empty()) continue;
    if (!expressions.empty()) {
      written.push_back(Object{{"feature", document.features[i].id},
                               {"entities", Array{}},
                               {"expressions", std::move(expressions)}});
      continue;
    }
    written.push_back(Object{{"feature", document.features[i].id},
                             {"entities", std::move(updated)},
                             {"expressions", Array{}}});
    ir::json::Object inputs = *document.features[i].inputs.if_object();
    for (auto& member : inputs)
      if (member.key == "entities") member.value = std::move(entities);
    document.features[i].inputs = std::move(inputs);
    changed = true;
  }
  if (!changed) return std::nullopt;
  auto text = ir::write_document(document);
  if (!text) return std::nullopt;
  return std::move(*text);
}
}  // namespace

Session::Session(kernel::IKernel& kernel, math::Tolerance tolerance)
    : kernel_(kernel), tolerance_(tolerance), evaluator_(kernel, {96, 64 * 1024 * 1024}) {
  auto empty = ir::write_document(ir::Document{});
  auto state = evaluate(std::move(*empty));
  if (!state) throw state.error();
  state_ = std::move(*state);
}

std::expected<Session::State, ir::Errors> Session::evaluate(std::string source, bool write_back) {
  auto document = ir::read_document(source);
  if (!document) return std::unexpected(document.error());
  auto normalized = ir::write_document(*document);
  if (!normalized) return std::unexpected(normalized.error());
  using Clock = std::chrono::steady_clock;
  const auto start = Clock::now();
  auto evaluated = evaluator_.evaluate(*normalized);
  const double evaluation_ms =
      std::chrono::duration<double, std::milli>(Clock::now() - start).count();
  if (!evaluated) return std::unexpected(evaluated.error());
  Array written;  // also lists sketches left as authored because of expression coordinates
  if (write_back) {
    if (auto rewritten = solved_source(*document, *evaluated, written)) {
      auto state = evaluate(std::move(*rewritten), false);
      if (state) state->build.written_back = std::move(written);
      return state;
    }
  }
  State state{std::move(*normalized), std::move(*document), std::move(*evaluated), {}, {}, {}, {}};
  auto& build = state.build;
  build.ok = state.evaluation.ok();
  for (const auto& f : state.evaluation.features) {
    Array errors;
    for (const auto& e : f.errors) {
      Object entry{{"code", e.code}, {"message", e.message}, {"pointer", e.pointer}};
      if (e.offset) entry.push_back({"offset", numeric(*e.offset)});
      errors.push_back(std::move(entry));
    }
    Object entry{{"id", f.id},
                 {"status", std::string(kStatus[static_cast<std::size_t>(f.status)])},
                 {"errors", std::move(errors)}};
    if (f.sketch) {
      Array under, redundant;
      for (const auto& id : f.sketch->underconstrained) under.push_back(id);
      for (const auto& pointer : f.sketch->redundant) redundant.push_back(pointer);
      entry.push_back({"sketch", Object{{"dof", numeric(f.sketch->dof)},
                                        {"fully_constrained", f.sketch->dof == 0},
                                        {"underconstrained", std::move(under)},
                                        {"redundant", std::move(redundant)},
                                        {"solved", f.sketch->solved}}});
    }
    build.features.push_back(std::move(entry));
  }
  // RFC 0013: an assembly publishes its occurrences' bodies at their solved placements, as
  // "occurrence/body"; the root part's own bodies are not separate results.
  std::vector<kernel_service::FeatureStatus> component_features;
  // An occurrence of the root evaluates it with its own overrides: then the assembly, not the
  // bare root build, decides whether the document builds (#330).
  const bool root_instanced = kernel_service::instances_root(state.document);
  if (!state.document.occurrences.empty() && (build.ok || root_instanced)) {
    auto assembly = kernel_service::evaluate_assembly(evaluator_, kernel_, state.document);
    state.evaluation.bodies.clear();
    if (!assembly) {
      build.ok = false;
      state.assembly_errors = assembly.error();
      build.assembly = Object{{"solved", false}, {"errors", error_entries(assembly.error())}};
    } else {
      build.ok = true;  // every occurrence, the root's included, built and the joints solved
      // The components' evaluations are this build's work too.
      state.evaluation.kernel_calls += assembly->kernel_calls;
      state.evaluation.cache_hits += assembly->cache_hits;
      state.evaluation.binding_misses += assembly->binding_misses;
      component_features = assembly->component_features;
      Array joints, occurrences;
      for (const auto& j : assembly->joints)
        joints.push_back(Object{{"id", j.id},
                                {"residual_mm", j.residual_mm},
                                {"residual_rad", j.residual_rad},
                                {"redundant", j.redundant}});
      for (const auto& o : assembly->occurrences) {
        Array rotation, translation{o.placement.translation.x, o.placement.translation.y,
                                    o.placement.translation.z};
        for (const double r : o.placement.rotation) rotation.push_back(r);
        Array bodies;
        for (std::size_t k = 0; k < o.bodies.size(); ++k) {
          const auto& [id, shape] = o.bodies[k];
          const auto& component_hash = o.source_hashes[k];
          kernel_service::BodyState body;
          body.shape = shape;
          body.names = o.body_names[k];
          // Identity of a placed body: its occurrence, the component body's state and the
          // exact placement, so a component edit that keeps volume and area still changes it.
          body.hash = ir::sha256(ir::json::canonical(
              Object{{"occurrence", o.id},
                     {"body", id},
                     {"source", component_hash ? Value(component_hash->hex()) : Value()},
                     {"rotation", rotation},
                     {"translation", translation},
                     {"volume", shape->measures().volume},
                     {"area", shape->measures().area}}));
          state.evaluation.bodies.emplace(o.id + "/" + id, std::move(body));
          bodies.push_back(o.id + "/" + id);
        }
        occurrences.push_back(Object{{"id", o.id},
                                     {"rotation", std::move(rotation)},
                                     {"translation", std::move(translation)},
                                     {"bodies", std::move(bodies)}});
      }
      build.assembly =
          Object{{"solved", assembly->solved},        {"dof", numeric(assembly->dof)},
                 {"loops", numeric(assembly->loops)}, {"iterations", numeric(assembly->iterations)},
                 {"joints", std::move(joints)},       {"occurrences", std::move(occurrences)}};
      state.assembly = std::move(*assembly);
    }
  }
  Array metrics;
  for (const auto& [id, body] : state.evaluation.bodies)
    if (live(body)) metrics.push_back(body_metrics(id, body));
  build.bodies = metrics;
  const auto usage = evaluator_.cache_usage();
  build.evidence = {{"version", 1.},
                    {"document_sha256", ir::sha256(state.source).hex()},
                    {"kernel", std::string(kernel_.identity())},
                    {"units", Object{{"length", "mm"}, {"area", "mm^2"}, {"volume", "mm^3"}}},
                    {"comparison_tolerances",
                     Object{{"linear_mm", tolerance_.linear}, {"angular_rad", tolerance_.angular}}},
                    {"feature_status_counts", feature_counts(state.evaluation, component_features)},
                    {"cache", Object{{"hits", numeric(state.evaluation.cache_hits)},
                                     {"kernel_calls", numeric(state.evaluation.kernel_calls)},
                                     {"binding_misses", numeric(state.evaluation.binding_misses)},
                                     {"entries", numeric(usage.entries)},
                                     {"estimated_bytes", numeric(usage.estimated_bytes)}}},
                    {"body_metrics", std::move(metrics)},
                    {"checks", Object{{"closed_solids", build.ok ? "kernel_validated" : "not_run"},
                                      {"step_roundtrip", "not_run"},
                                      {"physics", "not_run"}}},
                    {"measurement_error_bounds", nullptr},
                    {"timing_ms", Object{{"evaluation", evaluation_ms}}}};
  // Body deltas against the current state: hashes identify geometry, not ids alone.
  for (const auto& [id, body] : state.evaluation.bodies) {
    if (!live(body)) continue;
    const auto previous = state_.evaluation.bodies.find(id);
    if (previous == state_.evaluation.bodies.end() || !live(previous->second))
      build.added.push_back(id);
    else if (previous->second.hash != body.hash)
      build.changed.push_back(id);
  }
  for (const auto& [id, body] : state_.evaluation.bodies) {
    const auto next = state.evaluation.bodies.find(id);
    if (live(body) && (next == state.evaluation.bodies.end() || !live(next->second)))
      build.removed.push_back(id);
  }
  build.written_back = std::move(written);
  return state;
}

std::expected<Session::Build, ir::Errors> Session::commit(std::string source, bool rollback,
                                                          std::vector<std::string>& from,
                                                          std::vector<std::string>& to) {
  auto state = evaluate(std::move(source));
  if (!state) return std::unexpected(state.error());
  if (rollback && !state->build.ok) {
    ir::Errors errors;
    for (const auto& f : state->evaluation.features)
      errors.insert(errors.end(), f.errors.begin(), f.errors.end());
    errors.insert(errors.end(), state->assembly_errors.begin(), state->assembly_errors.end());
    return std::unexpected(std::move(errors));
  }
  // An adopted invalid document is not history: nothing can restore it.
  if (state_.errors.empty()) to.push_back(std::move(state_.source));
  if (&from != &to) from.clear();
  state_ = std::move(*state);
  return state_.build;
}

std::expected<Session::Build, ir::Errors> Session::replace(std::string source, bool rollback) {
  return commit(std::move(source), rollback, redo_, undo_);
}

std::expected<Session::Build, ir::Errors> Session::set_parameters(
    const std::map<std::string, std::string>& values, bool rollback) {
  auto source = apply_overrides(state_.source, values);
  if (!source) return std::unexpected(source.error());
  return commit(std::move(*source), rollback, redo_, undo_);
}

std::expected<Session::Build, ir::Errors> Session::undo() {
  if (undo_.empty()) return std::unexpected(ir::Errors{{"history", "nothing to undo", "", {}}});
  // The entry leaves the history only when the restored state builds.
  std::vector<std::string> none;
  auto result = commit(undo_.back(), false, none, redo_);
  if (result) undo_.pop_back();
  return result;
}

std::expected<Session::Build, ir::Errors> Session::redo() {
  if (redo_.empty()) return std::unexpected(ir::Errors{{"history", "nothing to redo", "", {}}});
  std::vector<std::string> none;
  auto result = commit(redo_.back(), false, none, undo_);
  if (result) redo_.pop_back();
  return result;
}

Object Session::inspect() const {
  const auto& d = state_.document;
  Array params, features;
  for (const auto& p : d.params) {
    Object entry{{"name", p.name}, {"value", p.value}};
    if (p.min) entry.push_back({"min", *p.min});
    if (p.max) entry.push_back({"max", *p.max});
    if (p.doc) entry.push_back({"doc", *p.doc});
    params.push_back(std::move(entry));
  }
  for (std::size_t i = 0; i < d.features.size(); ++i) {
    const auto& f = d.features[i];
    Object entry{{"id", f.id},
                 {"type", f.type},
                 {"name", optional_string(f.name)},
                 {"suppressed", f.suppressed.value_or(false)}};
    if (i < state_.build.features.size())
      for (const auto& m : *state_.build.features[i].if_object())
        if (m.key != "id") entry.push_back(m);
    features.push_back(std::move(entry));
  }
  Object result{{"document_sha256", ir::sha256(state_.source).hex()},
                {"units", Object{{"length", d.units.length}, {"angle", d.units.angle}}},
                {"meta", Object{{"name", optional_string(d.meta.name)},
                                {"doc", optional_string(d.meta.doc)}}},
                {"params", std::move(params)},
                {"history_end", optional_string(d.history_end)},
                {"features", std::move(features)},
                {"bodies", state_.build.bodies},
                {"ok", state_.build.ok},
                {"errors", error_entries(state_.errors)},
                {"kernel", std::string(kernel_.identity())},
                {"feature_status_counts", feature_counts(state_.evaluation)},
                {"can_undo", can_undo()},
                {"can_redo", can_redo()}};
  if (state_.build.assembly) result.push_back({"assembly", *state_.build.assembly});
  return result;
}

std::expected<Object, ir::Errors> Session::measure(const std::optional<std::string>& body) const {
  if (body) {
    const auto found = state_.evaluation.bodies.find(*body);
    if (found == state_.evaluation.bodies.end() || !live(found->second))
      return std::unexpected(ir::Errors{{"unknown_id", "no live body: " + *body, "", {}}});
    return body_metrics(*body, found->second);
  }
  kernel::Measures totals;
  Array bodies;
  for (const auto& [id, state] : state_.evaluation.bodies) {
    if (!live(state)) continue;
    const auto& m = state.shape->measures();
    totals.volume += m.volume;
    totals.area += m.area;
    totals.solids += m.solids;
    totals.faces += m.faces;
    totals.edges += m.edges;
    bodies.push_back(body_metrics(id, state));
  }
  return Object{{"bodies", std::move(bodies)},
                {"totals", Object{{"volume", totals.volume},
                                  {"area", totals.area},
                                  {"solids", numeric(totals.solids)},
                                  {"faces", numeric(totals.faces)},
                                  {"edges", numeric(totals.edges)}}}};
}

std::expected<Session::StepExport, ir::Errors> Session::export_step(
    const std::vector<std::string>& bodies) const {
  std::vector<kernel::ShapePtr> shapes;
  if (bodies.empty()) {
    for (const auto& [id, body] : state_.evaluation.bodies)
      if (live(body)) shapes.push_back(body.shape);
  } else {
    std::vector<std::string> seen;
    for (const auto& id : bodies) {
      if (std::ranges::find(seen, id) != seen.end()) continue;  // each body once
      seen.push_back(id);
      const auto found = state_.evaluation.bodies.find(id);
      if (found == state_.evaluation.bodies.end() || !live(found->second))
        return std::unexpected(ir::Errors{{"unknown_id", "no live body: " + id, "", {}}});
      shapes.push_back(found->second.shape);
    }
  }
  if (shapes.empty())
    return std::unexpected(ir::Errors{{"empty", "the document has no live solids", "", {}}});
  if (!state_.build.ok)
    return std::unexpected(ir::Errors{{"failed", "a feature failed; export refused", "", {}}});
  auto encoded = kernel_.export_step(shapes);
  if (!encoded)
    return std::unexpected(ir::Errors{{encoded.error().code, encoded.error().message, "", {}}});
  return StepExport{std::move(*encoded), shapes.size()};
}

std::expected<Session::MeshExport, ir::Errors> Session::export_obj(
    const std::vector<std::string>& bodies, kernel::MeshingOptions options) const {
  std::vector<std::pair<std::string, kernel::ShapePtr>> shapes;
  if (bodies.empty()) {
    for (const auto& [id, body] : state_.evaluation.bodies)
      if (live(body)) shapes.emplace_back(id, body.shape);
  } else {
    for (const auto& id : bodies) {
      if (std::ranges::any_of(shapes, [&](const auto& s) { return s.first == id; }))
        continue;  // each body once
      const auto found = state_.evaluation.bodies.find(id);
      if (found == state_.evaluation.bodies.end() || !live(found->second))
        return std::unexpected(ir::Errors{{"unknown_id", "no live body: " + id, "", {}}});
      shapes.emplace_back(id, found->second.shape);
    }
  }
  if (shapes.empty())
    return std::unexpected(ir::Errors{{"empty", "the document has no live solids", "", {}}});
  if (!state_.build.ok)
    return std::unexpected(ir::Errors{{"failed", "a feature failed; export refused", "", {}}});
  MeshExport result;
  result.bodies = shapes.size();
  result.obj = "# Nightfall derived mesh (mm); geometry is the part document, not this file\n";
  std::size_t offset = 1;  // OBJ indices are global and 1-based
  for (const auto& [id, shape] : shapes) {
    auto mesh = kernel_.tessellate(shape, options);
    if (!mesh)
      return std::unexpected(ir::Errors{{mesh.error().code, mesh.error().message, "", {}}});
    // The kernel bounds each body's mesh; the export bounds their sum before holding it.
    constexpr std::size_t kMaxTriangles = 4'000'000;
    if (result.triangles + mesh->indices.size() / 3 > kMaxTriangles)
      return std::unexpected(ir::Errors{
          {"too_large",
           "the export exceeds 4 million triangles; name fewer bodies or coarsen the deflection",
           "",
           {}}});
    result.obj += "o " + id + "\n";
    for (const auto& v : mesh->vertices)
      result.obj += "v " + ir::json::format_number(v.position.x) + ' ' +
                    ir::json::format_number(v.position.y) + ' ' +
                    ir::json::format_number(v.position.z) + "\n";
    for (const auto& v : mesh->vertices)
      result.obj += "vn " + ir::json::format_number(v.normal.x) + ' ' +
                    ir::json::format_number(v.normal.y) + ' ' +
                    ir::json::format_number(v.normal.z) + "\n";
    for (std::size_t i = 0; i + 2 < mesh->indices.size(); i += 3) {
      result.obj += "f";
      for (std::size_t k = 0; k < 3; ++k) {
        const auto index = std::to_string(offset + mesh->indices[i + k]);
        result.obj += ' ' + index + "//" + index;
      }
      result.obj += "\n";
    }
    result.vertices += mesh->vertices.size();
    result.triangles += mesh->indices.size() / 3;
    offset += mesh->vertices.size();
  }
  return result;
}

std::expected<Object, ir::Errors> Session::topology(const std::string& body) const {
  const auto found = state_.evaluation.bodies.find(body);
  if (found == state_.evaluation.bodies.end() || !live(found->second))
    return std::unexpected(ir::Errors{{"unknown_id", "no live body: " + body, "", {}}});
  const auto& state = found->second;
  auto faces = kernel_.faces(state.shape);
  if (!faces)
    return std::unexpected(ir::Errors{{faces.error().code, faces.error().message, "", {}}});
  auto names_of = [&](const kernel::Entity& entity) {
    Array names;
    for (const auto& n : state.names)
      if (n.entity && n.entity->kind() == entity.kind() && n.entity->same_as(entity)) {
        Object name{{"created_by", n.feature}, {"role", n.role}};
        if (!n.from.empty()) name.push_back({"from", n.from});
        names.push_back(std::move(name));
      }
    return names;
  };
  struct EdgeRecord {
    kernel::EntityPtr entity;
    Array faces;
  };
  std::vector<EdgeRecord> edges;
  Array face_entries;
  for (std::size_t i = 0; i < faces->size(); ++i) {
    const auto& info = (*faces)[i];
    Object entry{{"index", numeric(i)}};
    if (info.plane) {
      entry.push_back({"type", "plane"});
      entry.push_back({"origin", point(info.plane->origin)});
      entry.push_back({"normal", point(info.plane->normal)});
      entry.push_back({"x_axis", point(info.plane->x_axis)});
    } else if (info.cylinder) {
      entry.push_back({"type", "cylinder"});
      entry.push_back({"origin", point(info.cylinder->axis.origin)});
      entry.push_back({"axis", point(info.cylinder->axis.direction)});
      entry.push_back({"radius", info.cylinder->radius});
    } else {
      entry.push_back({"type", "other"});
    }
    entry.push_back({"names", names_of(*info.entity)});
    auto boundary = kernel_.edges(info.entity);
    if (!boundary)
      return std::unexpected(ir::Errors{{boundary.error().code, boundary.error().message, "", {}}});
    for (const auto& edge : *boundary) {
      auto known = std::find_if(edges.begin(), edges.end(),
                                [&](const EdgeRecord& r) { return r.entity->same_as(*edge); });
      if (known == edges.end()) known = edges.insert(edges.end(), EdgeRecord{edge, {}});
      known->faces.push_back(numeric(i));
    }
    entry.push_back({"edges", numeric(boundary->size())});
    face_entries.push_back(std::move(entry));
  }
  Array edge_entries;
  for (std::size_t i = 0; i < edges.size(); ++i)
    edge_entries.push_back(Object{
        {"index", numeric(i)}, {"faces", edges[i].faces}, {"names", names_of(*edges[i].entity)}});
  return Object{{"body", body},
                {"faces", std::move(face_entries)},
                {"edges", std::move(edge_entries)},
                {"note",
                 "indices are display order only; reference faces and edges through "
                 "selectors built from created_by, role and from"}};
}

namespace {
std::vector<kernel_service::PlacedBody> placed(const kernel_service::Evaluation& evaluation) {
  std::vector<kernel_service::PlacedBody> bodies;
  for (const auto& [id, body] : evaluation.bodies)
    if (live(body)) bodies.emplace_back(id, body.shape);
  return bodies;
}
std::expected<Object, ir::Errors> interference_of(kernel::IKernel& kernel,
                                                  const kernel_service::Evaluation& evaluation,
                                                  const std::vector<std::string>& only) {
  std::size_t candidates = 0;
  const auto found =
      kernel_service::check_interference(kernel, placed(evaluation), only, &candidates);
  if (!found) return std::unexpected(found.error());
  Array overlapping, touching;
  double total = 0;
  for (const auto& f : *found) {
    if (f.touching) {
      touching.push_back(Array{f.a, f.b});
    } else {
      overlapping.push_back(Object{{"a", f.a}, {"b", f.b}, {"volume", f.volume}});
      total += f.volume;
    }
  }
  return Object{{"overlapping", std::move(overlapping)},
                {"touching", std::move(touching)},
                {"overlap_volume", total},
                {"pairs_checked", numeric(candidates)}};
}
}  // namespace

std::expected<Object, ir::Errors> Session::interference(
    const std::vector<std::string>& only) const {
  for (const auto& id : only)
    if (std::ranges::none_of(state_.evaluation.bodies, [&](const auto& body) {
          return kernel_service::occurrence_of(body.first) == id;
        }))
      return std::unexpected(
          ir::Errors{{"unknown_id", "no live bodies in occurrence " + id, "", {}}});
  return interference_of(kernel_, state_.evaluation, only);
}

std::expected<Object, ir::Errors> Session::clearance(const std::string& a,
                                                     const std::string& b) const {
  const auto side = [&](const std::string& id) {
    std::vector<kernel_service::PlacedBody> bodies;
    for (const auto& body : placed(state_.evaluation))
      if (body.first == id || kernel_service::occurrence_of(body.first) == id)
        bodies.push_back(body);
    return bodies;
  };
  const auto sa = side(a), sb = side(b);
  if (sa.empty() || sb.empty())
    return std::unexpected(
        ir::Errors{{"unknown_id", "no live body or occurrence " + (sa.empty() ? a : b), "", {}}});
  const auto c = kernel_service::measure_clearance(kernel_, sa, sb);
  if (!c) return std::unexpected(c.error());
  return Object{{"distance", c->distance},
                {"a", c->a},
                {"b", c->b},
                {"on_a", point(c->on_a)},
                {"on_b", point(c->on_b)}};
}

std::map<std::string, double> Session::densities() const {
  std::map<std::string, double> density;
  for (const auto& o : state_.document.occurrences) {
    const auto c = std::ranges::find(state_.document.components, o.component, &ir::Component::id);
    if (o.density)
      density[o.id] = *o.density;
    else if (c != state_.document.components.end() && c->density)
      density[o.id] = *c->density;
  }
  return density;
}

std::expected<kernel_service::UrdfExport, ir::Errors> Session::export_urdf(
    const std::string& mesh_dir, const std::string& name) const {
  if (!state_.assembly)
    return std::unexpected(ir::Errors{
        {"not_an_assembly", "the document has no solved assembly to export", "/occurrences", {}}});
  return kernel_service::export_urdf(kernel_, state_.document, *state_.assembly, densities(),
                                     mesh_dir, name);
}

std::expected<Object, ir::Errors> Session::mass_properties(
    const std::optional<std::string>& occurrence) const {
  const auto density = densities();
  std::vector<std::string> only;
  if (occurrence) {
    if (std::ranges::find(state_.document.occurrences, *occurrence, &ir::Occurrence::id) ==
        state_.document.occurrences.end())
      return std::unexpected(ir::Errors{{"unknown_id", "no occurrence " + *occurrence, "", {}}});
    only.push_back(*occurrence);
  }
  // Kernel failures of an existing occurrence are reported as they are.
  const auto m = kernel_service::mass_properties(kernel_, placed(state_.evaluation), density, only);
  if (!m) return std::unexpected(m.error());
  const auto tensor = [](const std::array<double, 9>& t) {
    return Array{Array{t[0], t[1], t[2]}, Array{t[3], t[4], t[5]}, Array{t[6], t[7], t[8]}};
  };
  const auto entry = [&](const kernel_service::MassReport& r) {
    Object o{{"id", r.id}, {"volume", r.volume}, {"center", point(r.centroid)}};
    if (r.density) {
      o.push_back({"density", *r.density});
      o.push_back({"mass", r.mass});
      o.push_back({"inertia", tensor(r.inertia)});
    } else {
      o.push_back({"density", nullptr});
    }
    return Value(std::move(o));
  };
  Array occurrences, without;
  for (const auto& r : m->occurrences) occurrences.push_back(entry(r));
  for (const auto& id : m->without_density) without.push_back(id);
  Object result{{"units", Object{{"volume", "mm^3"},
                                 {"center", "mm"},
                                 {"density", "kg/m^3"},
                                 {"mass", "kg"},
                                 {"inertia", "kg m^2 about the center, assembly axes"}}},
                {"occurrences", std::move(occurrences)},
                {"without_density", std::move(without)}};
  if (m->total.mass > 0)
    result.push_back({"total", Object{{"mass", m->total.mass},
                                      {"center", point(m->total.centroid)},
                                      {"inertia", tensor(m->total.inertia)}}});
  return result;
}

std::expected<Object, ir::Errors> Session::sweep(const std::string& joint, double from, double to,
                                                 std::size_t steps, bool interference,
                                                 const std::vector<std::string>& report) {
  const auto& joints = state_.document.joints;
  const auto found = std::ranges::find(joints, joint, &ir::Joint::id);
  if (found == joints.end())
    return std::unexpected(ir::Errors{{"unknown_id", "no joint " + joint, "/joints", {}}});
  if (steps < 1 || steps > 360 || !std::isfinite(from) || !std::isfinite(to))
    return std::unexpected(
        ir::Errors{{"invalid_value", "a sweep needs finite bounds and 1 to 360 steps", "", {}}});
  const auto rotary = found->kind == "revolute" || found->kind == "cylindrical";
  if (!rotary && found->kind != "slider")
    return std::unexpected(ir::Errors{
        {"unsupported", "sweeps drive revolute, cylindrical and slider joints", "/joints", {}}});
  const auto unit = rotary ? state_.document.units.angle : state_.document.units.length;
  const auto index = static_cast<std::size_t>(found - joints.begin());
  auto document = ir::json::parse(state_.source).value();
  // A cylindrical joint may drive [angle, length]: the sweep replaces the angle and keeps the
  // authored length.
  auto pointer = "/joints/" + std::to_string(index) + "/value";
  std::string op = "add";
  if (const auto* authored = (*document.find("joints")->if_array())[index].find("value");
      authored && authored->if_array() && !authored->if_array()->empty()) {
    pointer += "/0";
    op = "replace";
  }
  Array results;
  std::size_t solved = 0, clashing = 0;
  for (std::size_t k = 0; k <= steps; ++k) {
    const double value = from + (to - from) * static_cast<double>(k) / static_cast<double>(steps);
    auto patched = ir::apply_patch(
        document, Array{Object{{"op", op},
                               {"path", pointer},
                               {"value", ir::json::format_number(value) + " " + unit}}});
    if (!patched) return std::unexpected(patched.error());
    Object entry{{"value", value}};
    auto state = evaluate(ir::json::write(*patched), false);
    if (!state || !state->build.ok || !state->build.assembly) {
      ir::Errors errors = state ? state->assembly_errors : state.error();
      if (state)  // a failed feature fails the build before the assembly is solved
        for (const auto& f : state->evaluation.features)
          errors.insert(errors.end(), f.errors.begin(), f.errors.end());
      entry.push_back({"solved", false});
      entry.push_back({"errors", error_entries(errors)});
      results.push_back(std::move(entry));
      continue;
    }
    ++solved;
    const Value assembly = *state->build.assembly;
    entry.push_back({"solved", true});
    entry.push_back({"dof", *assembly.find("dof")});
    Object placements;
    for (const auto& o : *assembly.find("occurrences")->if_array()) {
      const auto& id = *o.find("id")->if_string();
      if (!report.empty() && std::ranges::find(report, id) == report.end()) continue;
      Array matrix = *o.find("rotation")->if_array();
      for (const auto& t : *o.find("translation")->if_array()) matrix.push_back(t);
      placements.push_back({id, std::move(matrix)});
    }
    entry.push_back({"placements", std::move(placements)});
    if (interference) {
      auto clash = interference_of(kernel_, state->evaluation, {});
      if (!clash) return std::unexpected(clash.error());
      if (!Value(*clash).find("overlapping")->if_array()->empty()) ++clashing;
      entry.push_back({"interference", std::move(*clash)});
    }
    results.push_back(std::move(entry));
  }
  Object summary{{"joint", joint},
                 {"unit", unit},
                 {"steps", std::move(results)},
                 {"solved_steps", numeric(solved)}};
  if (interference) summary.push_back({"steps_with_overlap", numeric(clashing)});
  return summary;
}

}  // namespace nf::cli
