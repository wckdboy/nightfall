// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "study.hpp"

#include <unistd.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>

#include "geometry.hpp"
#include "nf/ir/expression.hpp"
#include "nf/ir/parameters.hpp"
#include "physics.hpp"
#include "units.hpp"

namespace nf::study {
namespace {
/// A private directory (mode 0700, unique name) for one run's files, removed on every exit.
struct ScratchDirectory {
  std::filesystem::path path;
  ScratchDirectory() {
    auto pattern = (std::filesystem::temp_directory_path() / "nf-study-XXXXXX").string();
    if (!mkdtemp(pattern.data())) throw std::runtime_error("cannot create a scratch directory");
    path = pattern;
  }
  ScratchDirectory(const ScratchDirectory&) = delete;
  ScratchDirectory& operator=(const ScratchDirectory&) = delete;
  ~ScratchDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
};
/// SHA-256 of everything that defines a mesh: nodes, tetrahedra, boundary triangles and their
/// faces, each array framed by its length.
std::string mesh_digest(const PlainMesh& mesh) {
  std::string bytes;
  const auto append = [&](const auto& values) {
    const std::uint64_t n = values.size();
    bytes.append(reinterpret_cast<const char*>(&n), sizeof n);
    bytes.append(reinterpret_cast<const char*>(values.data()),
                 values.size() * sizeof(values.front()));
  };
  append(mesh.points);
  append(mesh.tets);
  append(mesh.triangles);
  append(mesh.faces);
  return ir::sha256(bytes).hex();
}
using ir::json::Array;
using ir::json::Object;
using ir::json::Value;

struct Failure {
  ir::Errors errors;
};
[[noreturn]] void fail(std::string pointer, std::string message, std::string code = "invalid") {
  throw Failure{{{std::move(code), std::move(message), std::move(pointer), {}}}};
}
const std::string& text(const Value& v, const std::string& pointer) {
  if (!v.if_string()) fail(pointer, "expected a string");
  return *v.if_string();
}
double value(const Value& v, Dimension d, const std::string& pointer) {
  auto parsed = parse_value(text(v, pointer), d);
  if (!parsed) fail(pointer, parsed.error(), "unit");
  return *parsed;
}
Array vec(const std::array<double, 3>& v) { return {v[0], v[1], v[2]}; }

/// The faces a {created_by, role, from} selector names on a body, as mesher face numbers.
std::vector<int> faces(const kernel_service::BodyState& body, const Value& selector,
                       const std::string& pointer) {
  if (!selector.if_object()) fail(pointer, "expected a face selector");
  for (const auto& m : *selector.if_object())
    if (m.key != "created_by" && m.key != "role" && m.key != "from" && !m.key.starts_with("x-"))
      fail(pointer + "/" + m.key, "study faces support {created_by, role, from} selectors only",
           "unsupported");
  const auto field = [&](const char* key) {
    const auto* v = selector.find(key);
    return v ? text(*v, pointer + "/" + key) : std::string();
  };
  const auto producer = field("created_by"), role = field("role"), from = field("from");
  std::vector<int> numbers;
  for (const auto& name : body.names) {
    if (name.feature != producer || (!role.empty() && name.role != role) ||
        (!from.empty() && name.from != from) || !name.entity ||
        name.entity->kind() != kernel::EntityKind::kFace)
      continue;
    auto n = face_number(name.entity);
    if (!n) fail(pointer, n.error().message);
    if (std::ranges::find(numbers, *n) == numbers.end()) numbers.push_back(*n);
  }
  if (numbers.empty()) fail(pointer, "the selector matches no face of the study body");
  return numbers;
}

struct Prepared {
  std::string kind;
  StructuralInput structural;
  ThermalInput thermal;
};
Prepared prepare(const Value& study, const kernel_service::BodyState& body,
                 const std::string& base) {
  Prepared p;
  p.kind = text(*study.find("kind"), base + "/kind");
  const auto& material = *study.find("material");
  const auto boundaries = *study.find("boundaries")->if_array();
  const bool structural = p.kind == "static_structural";
  if (structural) {
    const auto* E = material.find("youngs_modulus");
    const auto* nu = material.find("poisson_ratio");
    if (!E || !nu || !nu->if_number())
      fail(base + "/material", "a structural study needs youngs_modulus and poisson_ratio");
    p.structural.youngs_modulus = value(*E, Dimension::kStress, base + "/material/youngs_modulus");
    p.structural.poisson_ratio = *nu->if_number();
  } else {
    const auto* k = material.find("conductivity");
    if (!k) fail(base + "/material", "a thermal study needs a conductivity");
    p.thermal.conductivity = value(*k, Dimension::kConductivity, base + "/material/conductivity");
  }
  for (std::size_t i = 0; i < boundaries.size(); ++i) {
    const auto& b = boundaries[i];
    const auto at = base + "/boundaries/" + std::to_string(i);
    const auto type = text(*b.find("type"), at + "/type");
    const auto selected = faces(body, *b.find("faces"), at + "/faces");
    const auto* v = b.find("value");
    const auto need = [&](const Value* field, const char* name) -> const Value& {
      if (!field) fail(at, type + " needs " + name);
      return *field;
    };
    if (structural && type == "fixed") {
      p.structural.fixed.insert(p.structural.fixed.end(), selected.begin(), selected.end());
    } else if (structural && type == "force") {
      const auto& list = need(v, "a value");
      if (!list.if_array() || list.if_array()->size() != 3)
        fail(at + "/value", "a force is three components");
      FaceForce force{selected, {}};
      for (std::size_t c = 0; c < 3; ++c)
        force.newtons[c] =
            value((*list.if_array())[c], Dimension::kForce, at + "/value/" + std::to_string(c));
      p.structural.forces.push_back(force);
    } else if (structural && type == "pressure") {
      p.structural.pressures.push_back(
          {selected, value(need(v, "a value"), Dimension::kStress, at + "/value")});
    } else if (!structural && type == "temperature") {
      p.thermal.temperatures.push_back(
          {selected, value(need(v, "a value"), Dimension::kTemperature, at + "/value")});
    } else if (!structural && type == "heat_flux") {
      p.thermal.fluxes.push_back(
          {selected, value(need(v, "a value"), Dimension::kHeatFlux, at + "/value")});
    } else if (!structural && type == "convection") {
      p.thermal.convection.push_back(
          {selected,
           value(need(b.find("film"), "a film"), Dimension::kFilmCoefficient, at + "/film"),
           value(need(b.find("ambient"), "an ambient"), Dimension::kTemperature, at + "/ambient")});
    } else {
      fail(at + "/type", type + " does not apply to a " + p.kind + " study");
    }
  }
  return p;
}

/// Quantities of one run, by name.
std::map<std::string, double> quantities(const Prepared& p, const PlainMesh& mesh, Object& out) {
  std::map<std::string, double> q;
  if (p.kind == "static_structural") {
    auto r = solve_structural(mesh, p.structural);
    if (!r) fail("", r.error(), "solver");
    q = {{"max_displacement", r->max_displacement}, {"max_von_mises", r->max_von_mises}};
    out.push_back({"applied_force", vec(r->applied)});
    out.push_back({"reaction_force", vec(r->reaction)});
    out.push_back({"solver", Object{{"unknowns", static_cast<double>(r->solver.unknowns)},
                                    {"iterations", static_cast<double>(r->solver.iterations)},
                                    {"relative_residual", r->solver.relative_residual}}});
  } else {
    auto r = solve_thermal(mesh, p.thermal);
    if (!r) fail("", r.error(), "solver");
    q = {{"max_temperature", r->max_temperature}, {"min_temperature", r->min_temperature}};
    Array heat;
    for (const double h : r->heat_in) heat.push_back(h);
    out.push_back({"heat_in", std::move(heat)});
    out.push_back({"solver", Object{{"unknowns", static_cast<double>(r->solver.unknowns)},
                                    {"iterations", static_cast<double>(r->solver.iterations)},
                                    {"relative_residual", r->solver.relative_residual}}});
  }
  return q;
}
const std::map<std::string, std::pair<const char*, Dimension>> kQuantities{
    {"max_von_mises", {"MPa", Dimension::kStress}},
    {"max_temperature", {"K", Dimension::kTemperature}},
    {"min_temperature", {"K", Dimension::kTemperature}}};
}  // namespace

std::expected<Object, ir::Errors> run_study(kernel_service::Evaluator& evaluator, kernel::IKernel&,
                                            const std::string& source, const std::string& id,
                                            const RunOptions& options) {
  try {
    auto document = ir::read_document(source);
    if (!document) return std::unexpected(document.error());
    std::size_t index = 0;
    while (index < document->studies.size() &&
           *document->studies[index].find("id")->if_string() != id)
      ++index;
    if (index == document->studies.size()) fail("/studies", "no study " + id, "unknown_id");
    const auto base = "/studies/" + std::to_string(index);
    const auto& study = document->studies[index];
    auto evaluated = evaluator.evaluate(source);
    if (!evaluated) return std::unexpected(evaluated.error());
    if (!evaluated->ok()) fail(base + "/body", "the document does not build", "input_failed");
    const auto body_id = text(*study.find("body"), base + "/body");
    const auto found = evaluated->bodies.find(body_id);
    if (found == evaluated->bodies.end() || !found->second.shape)
      fail(base + "/body", "no live body " + body_id, "unknown_id");
    const auto& body = found->second;
    const auto prepared = prepare(study, body, base);
    auto parameters = ir::evaluate_parameters(document->params);
    if (!parameters) return std::unexpected(parameters.error());
    const auto size = ir::evaluate_input(*study.find("mesh")->find("size"), ir::kLength,
                                         document->units, parameters->values);
    if (!size) {
      auto error = size.error();
      error.pointer = base + "/mesh/size";
      return std::unexpected(ir::Errors{std::move(error)});
    }
    std::vector<double> sizes = options.refine;
    if (sizes.empty()) sizes.push_back(options.size.value_or(*size));
    const ScratchDirectory scratch;
    const auto path = scratch.path / "body.brep";
    if (auto written = write_brep(body.shape, path.string()); !written)
      fail(base + "/body", written.error().message);
    Array runs;
    std::vector<std::map<std::string, double>> values;
    for (const double h : sizes) {
      PlainMesh mesh;
      try {
        mesh = mesh_brep(path.string(), h);
      } catch (const std::exception& e) {
        fail(base + "/mesh", e.what(), "mesher");
      }
      Object run{{"mesh_size", h},
                 {"nodes", static_cast<double>(mesh.node_count())},
                 {"tetrahedra", static_cast<double>(mesh.tet_count())},
                 {"mesh_sha256", mesh_digest(mesh)}};
      const auto q = quantities(prepared, mesh, run);
      Object listed;
      for (const auto& [name, v] : q) listed.push_back({name, v});
      run.insert(run.begin() + 1, {"quantities", std::move(listed)});
      values.push_back(q);
      runs.push_back(std::move(run));
    }
    const auto& final = values.back();
    Array criteria;
    bool passed = true;
    if (const auto* list = study.find("criteria"))
      for (std::size_t i = 0; i < list->if_array()->size(); ++i) {
        const auto& c = (*list->if_array())[i];
        const auto at = base + "/criteria/" + std::to_string(i);
        const auto name = text(*c.find("quantity"), at + "/quantity");
        if (!final.contains(name)) fail(at + "/quantity", name + " is not reported by this study");
        const auto* below = c.find("below");
        const auto* above = c.find("above");
        if (!below && !above) fail(at, "a criterion needs below or above");
        const auto limit = [&](const Value& v, const std::string& p) {
          if (name == "max_displacement") {
            auto mm = ir::evaluate_input(v, ir::kLength, document->units, parameters->values);
            if (!mm) fail(p, mm.error().message, "unit");
            return *mm;
          }
          return value(v, kQuantities.at(name).second, p);
        };
        const double actual = final.at(name);
        Object entry{{"quantity", name}, {"value", actual}};
        bool ok = true;
        if (below) {
          const double l = limit(*below, at + "/below");
          entry.push_back({"below", l});
          ok = ok && actual < l;
        }
        if (above) {
          const double l = limit(*above, at + "/above");
          entry.push_back({"above", l});
          ok = ok && actual > l;
        }
        entry.push_back({"passed", ok});
        passed = passed && ok;
        criteria.push_back(std::move(entry));
      }
    Object result{{"study", id},
                  {"kind", prepared.kind},
                  {"body", body_id},
                  {"units", Object{{"length", "mm"},
                                   {"displacement", "mm"},
                                   {"stress", "MPa"},
                                   {"force", "N"},
                                   {"temperature", "K"},
                                   {"heat", "W"}}},
                  {"quantities", *runs.back().find("quantities")},
                  {"criteria", std::move(criteria)},
                  {"passed", passed},
                  {"runs", runs},
                  {"provenance", Object{{"document_sha256", ir::sha256(source).hex()},
                                        {"body_hash", body.hash ? body.hash->hex() : std::string()},
                                        {"mesher", mesher_identity()},
                                        {"solver", solver_identity()}}}};
    // Observed order of convergence for three sizes in a constant ratio.
    if (values.size() == 3 && sizes[0] / sizes[1] > 1 &&
        std::abs(sizes[0] / sizes[1] - sizes[1] / sizes[2]) < 1e-9 * sizes[0] / sizes[1]) {
      Object orders;
      for (const auto& [name, v] : values[2]) {
        const double d1 = values[0].at(name) - values[1].at(name), d2 = values[1].at(name) - v;
        if (d1 != 0 && d2 != 0 && d1 / d2 > 0)
          orders.push_back({name, std::log(d1 / d2) / std::log(sizes[0] / sizes[1])});
      }
      result.push_back({"convergence_order", std::move(orders)});
    }
    return result;
  } catch (Failure& f) {
    return std::unexpected(std::move(f.errors));
  }
}
}  // namespace nf::study
