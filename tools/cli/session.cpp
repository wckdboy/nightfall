// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "session.hpp"

#include <unistd.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>

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

Object feature_counts(const kernel_service::Evaluation& evaluation) {
  std::array<std::size_t, 5> counts{};
  for (const auto& f : evaluation.features) ++counts[static_cast<std::size_t>(f.status)];
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
  state_ = State{std::move(source), {}, {}, {}, std::move(errors)};
  undo_.clear();
  redo_.clear();
}

Object Session::Build::json() const {
  return {{"ok", ok},
          {"evidence", evidence},
          {"features", features},
          {"bodies", bodies},
          {"changed_bodies", changed},
          {"added_bodies", added},
          {"removed_bodies", removed}};
}

Session::Session(kernel::IKernel& kernel, math::Tolerance tolerance)
    : kernel_(kernel), tolerance_(tolerance), evaluator_(kernel, {96, 64 * 1024 * 1024}) {
  auto empty = ir::write_document(ir::Document{});
  auto state = evaluate(std::move(*empty));
  if (!state) throw state.error();
  state_ = std::move(*state);
}

std::expected<Session::State, ir::Errors> Session::evaluate(std::string source) {
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
  State state{std::move(*normalized), std::move(*document), std::move(*evaluated), {}, {}};
  auto& build = state.build;
  build.ok = state.evaluation.ok();
  for (const auto& f : state.evaluation.features) {
    Array errors;
    for (const auto& e : f.errors) {
      Object entry{{"code", e.code}, {"message", e.message}, {"pointer", e.pointer}};
      if (e.offset) entry.push_back({"offset", numeric(*e.offset)});
      errors.push_back(std::move(entry));
    }
    build.features.push_back(
        Object{{"id", f.id},
               {"status", std::string(kStatus[static_cast<std::size_t>(f.status)])},
               {"errors", std::move(errors)}});
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
                    {"feature_status_counts", feature_counts(state.evaluation)},
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
  return {{"document_sha256", ir::sha256(state_.source).hex()},
          {"units", Object{{"length", d.units.length}, {"angle", d.units.angle}}},
          {"meta",
           Object{{"name", optional_string(d.meta.name)}, {"doc", optional_string(d.meta.doc)}}},
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
      const auto found = state_.evaluation.bodies.find(id);
      if (found == state_.evaluation.bodies.end() || !live(found->second))
        return std::unexpected(ir::Errors{{"unknown_id", "no live body: " + id, "", {}}});
      shapes.emplace_back(id, found->second.shape);
    }
  }
  if (shapes.empty())
    return std::unexpected(ir::Errors{{"empty", "the document has no live solids", "", {}}});
  MeshExport result;
  result.obj = "# Nightfall derived mesh (mm); geometry is the part document, not this file\n";
  std::size_t offset = 1;  // OBJ indices are global and 1-based
  for (const auto& [id, shape] : shapes) {
    auto mesh = kernel_.tessellate(shape, options);
    if (!mesh)
      return std::unexpected(ir::Errors{{mesh.error().code, mesh.error().message, "", {}}});
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

}  // namespace nf::cli
