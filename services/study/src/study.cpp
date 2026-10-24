// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "study.hpp"

#include <unistd.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>

#include "geometry.hpp"
#include "nf/ir/expression.hpp"
#include "nf/ir/parameters.hpp"
#include "physics.hpp"
#include "units.hpp"

namespace nf::study {
namespace {
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
/// The member `key` of `object`, failing at `pointer` when it is missing (specs from files are
/// not schema-checked as documents are).
const Value& required(const Value& object, const char* key, const std::string& pointer) {
  const auto* v = object.if_object() ? object.find(key) : nullptr;
  if (!v) fail(pointer, std::string("expected ") + key);
  return *v;
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
/// A boundary's `faces` as mesher face numbers; `pointer` locates it for errors.
using FaceResolver =
    std::function<std::vector<int>(const Value& faces, const std::string& pointer)>;
/// A length value (a criterion's limit, a mesh size) in mm.
using LengthReader = std::function<double(const Value& length, const std::string& pointer)>;

Prepared prepare(const Value& study, const FaceResolver& resolve, const std::string& base) {
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
    const auto type = text(required(b, "type", at), at + "/type");
    const auto selected = resolve(required(b, "faces", at), at + "/faces");
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
/// The surface of `mesh` with nodal values for display (#321): surface nodes renumbered from 0,
/// the boundary triangles, and per node the von Mises stress and the displacement.
Object surface_field(const PlainMesh& mesh, const StructuralResult& r) {
  std::map<int, double> number;
  Array points, triangles, stress, displacement;
  for (const int node : mesh.triangles) {
    auto [at, fresh] = number.try_emplace(node, static_cast<double>(number.size()));
    if (fresh) {
      const auto n = static_cast<std::size_t>(node);
      for (std::size_t c = 0; c < 3; ++c) {
        points.push_back(mesh.points[3 * n + c]);
        displacement.push_back(r.nodal_displacement.at(3 * n + c));
      }
      stress.push_back(r.nodal_von_mises.at(n));
    }
    triangles.push_back(at->second);
  }
  return {{"kind", "static_structural"},
          {"units", Object{{"length", "mm"}, {"stress", "MPa"}, {"displacement", "mm"}}},
          {"points", std::move(points)},
          {"triangles", std::move(triangles)},
          {"von_mises", std::move(stress)},
          {"displacement", std::move(displacement)}};
}
std::map<std::string, double> quantities(const Prepared& p, const PlainMesh& mesh, Object& out,
                                         Object* field) {
  std::map<std::string, double> q;
  if (p.kind == "static_structural") {
    auto r = solve_structural(mesh, p.structural);
    if (!r) fail("", r.error(), "solver");
    if (field) *field = surface_field(mesh, *r);
    q = {{"max_displacement", r->max_displacement}, {"max_von_mises", r->max_von_mises}};
    out.push_back({"applied_force", vec(r->applied)});
    out.push_back({"reaction_force", vec(r->reaction)});
    out.push_back({"solver", Object{{"unknowns", static_cast<double>(r->solver.unknowns)},
                                    {"iterations", static_cast<double>(r->solver.iterations)},
                                    {"relative_residual", r->solver.relative_residual},
                                    {"backward_error", r->solver.backward_error},
                                    {"method", r->solver.method}}});
  } else {
    auto r = solve_thermal(mesh, p.thermal);
    if (!r) fail("", r.error(), "solver");
    q = {{"max_temperature", r->max_temperature}, {"min_temperature", r->min_temperature}};
    Array heat;
    for (const double h : r->heat_in) heat.push_back(h);
    out.push_back({"heat_in", std::move(heat)});
    out.push_back({"solver", Object{{"unknowns", static_cast<double>(r->solver.unknowns)},
                                    {"iterations", static_cast<double>(r->solver.iterations)},
                                    {"relative_residual", r->solver.relative_residual},
                                    {"backward_error", r->solver.backward_error},
                                    {"method", r->solver.method}}});
  }
  return q;
}
const std::map<std::string, std::pair<const char*, Dimension>> kQuantities{
    {"max_von_mises", {"MPa", Dimension::kStress}},
    {"max_temperature", {"K", Dimension::kTemperature}},
    {"min_temperature", {"K", Dimension::kTemperature}}};
/// Meshes `shape` at each size, solves the prepared study, checks its criteria and reports
/// quantities, runs and the observed convergence order.
Object solve(const Value& study, const Prepared& prepared, const kernel::ShapePtr& shape,
             const std::vector<double>& sizes, const LengthReader& length, const std::string& base,
             const std::string& field_path = {}) {
  const auto path =
      std::filesystem::temp_directory_path() / ("nf-study-" + std::to_string(getpid()) + ".brep");
  if (auto written = write_brep(shape, path.string()); !written)
    fail(base + "/body", written.error().message);
  Array runs;
  std::vector<std::map<std::string, double>> values;
  Object field;
  for (std::size_t i = 0; i < sizes.size(); ++i) {
    const double h = sizes[i];
    PlainMesh mesh;
    try {
      mesh = mesh_brep(path.string(), h);
    } catch (const std::exception& e) {
      std::filesystem::remove(path);
      fail(base + "/mesh", e.what(), "mesher");
    }
    Object run{{"mesh_size", h},
               {"nodes", static_cast<double>(mesh.node_count())},
               {"tetrahedra", static_cast<double>(mesh.tet_count())},
               {"mesh_sha256",
                ir::sha256(std::string_view(reinterpret_cast<const char*>(mesh.points.data()),
                                            mesh.points.size() * sizeof(double)))
                    .hex()}};
    const bool last = i + 1 == sizes.size();
    const auto q = quantities(prepared, mesh, run, last && !field_path.empty() ? &field : nullptr);
    Object listed;
    for (const auto& [name, v] : q) listed.push_back({name, v});
    run.insert(run.begin() + 1, {"quantities", std::move(listed)});
    values.push_back(q);
    runs.push_back(std::move(run));
  }
  std::filesystem::remove(path);
  if (!field_path.empty()) {
    if (field.empty()) fail(base + "/kind", "only a static_structural study has a field to write");
    std::ofstream out(field_path, std::ios::binary);
    out << ir::json::write(Value(std::move(field)));
    if (!out) fail("", "cannot write the field to " + field_path, "io");
  }
  const auto& final = values.back();
  Array criteria;
  bool passed = true;
  if (const auto* list = study.find("criteria")) {
    if (!list->if_array()) fail(base + "/criteria", "expected an array");
    for (std::size_t i = 0; i < list->if_array()->size(); ++i) {
      const auto& c = (*list->if_array())[i];
      const auto at = base + "/criteria/" + std::to_string(i);
      const auto name = text(required(c, "quantity", at), at + "/quantity");
      if (!final.contains(name)) fail(at + "/quantity", name + " is not reported by this study");
      const auto* below = c.find("below");
      const auto* above = c.find("above");
      if (!below && !above) fail(at, "a criterion needs below or above");
      const auto limit = [&](const Value& v, const std::string& p) {
        if (name == "max_displacement") return length(v, p);
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
  }
  Object result{{"kind", prepared.kind},
                {"units", Object{{"length", "mm"},
                                 {"displacement", "mm"},
                                 {"stress", "MPa"},
                                 {"force", "N"},
                                 {"temperature", "K"},
                                 {"heat", "W"}}},
                {"quantities", *runs.back().find("quantities")},
                {"criteria", std::move(criteria)},
                {"passed", passed},
                {"runs", runs}};
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
}
std::vector<double> sizes(const RunOptions& options, double study_size,
                          const std::string& pointer) {
  if (!(study_size > 0)) fail(pointer, "the mesh size must be positive");
  std::vector<double> result = options.refine;
  if (result.empty()) result.push_back(options.size.value_or(study_size));
  return result;
}
ir::Errors errors_of(Failure& f) { return std::move(f.errors); }
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
    const auto prepared = prepare(
        study, [&](const Value& v, const std::string& p) { return faces(body, v, p); }, base);
    auto parameters = ir::evaluate_parameters(document->params);
    if (!parameters) return std::unexpected(parameters.error());
    const LengthReader length = [&](const Value& v, const std::string& p) {
      auto mm = ir::evaluate_input(v, ir::kLength, document->units, parameters->values);
      if (!mm) fail(p, mm.error().message, "unit");
      return *mm;
    };
    const double size = length(*study.find("mesh")->find("size"), base + "/mesh/size");
    auto result = solve(study, prepared, body.shape, sizes(options, size, base + "/mesh/size"),
                        length, base, options.field);
    result.insert(result.begin(), {"body", body_id});
    result.insert(result.begin(), {"study", id});
    result.push_back(
        {"provenance", Object{{"document_sha256", ir::sha256(source).hex()},
                              {"body_hash", body.hash ? body.hash->hex() : std::string()},
                              {"mesher", mesher_identity()},
                              {"solver", solver_identity()}}});
    return result;
  } catch (Failure& f) {
    return std::unexpected(errors_of(f));
  }
}

std::expected<Object, ir::Errors> run_step_study(kernel::IKernel& kernel, const std::string& step,
                                                 const std::string& spec,
                                                 const RunOptions& options) {
  try {
    const auto parsed = ir::json::parse(spec);
    if (!parsed || !parsed->if_object()) fail("", "the study is not a JSON object");
    const auto& study = *parsed;
    for (const char* key : {"kind", "material", "boundaries", "mesh"})
      if (!study.find(key)) fail("", std::string("the study needs ") + key);
    if (!study.find("boundaries")->if_array()) fail("/boundaries", "expected an array");
    auto solids = kernel.import_step(step);
    if (!solids) fail("", solids.error().message, "step_read");
    if (solids->size() != 1) fail("", "a STEP study is of exactly one solid", "step_read");
    const auto shape = solids->front();
    std::vector<kernel::EntityPtr> faces;
    auto listed = kernel.faces(shape);
    if (!listed) fail("", listed.error().message, "step_read");
    for (auto& f : *listed) faces.push_back(std::move(f.entity));
    const auto& m = shape->measures();
    const double size =
        std::max({m.maximum.x - m.minimum.x, m.maximum.y - m.minimum.y, m.maximum.z - m.minimum.z});
    // Points picked on the part's own faces lie on them to within its export's precision.
    const double tolerance = 1e-4 + 1e-7 * size;
    const FaceResolver resolve = [&](const Value& selector, const std::string& pointer) {
      const auto* points = selector.find("points");
      if (!selector.if_object() || !points || !points->if_array() || points->if_array()->empty())
        fail(pointer, "a STEP study picks faces by {\"points\": [[x, y, z], ...]}");
      std::vector<int> numbers;
      for (std::size_t i = 0; i < points->if_array()->size(); ++i) {
        const auto at = pointer + "/points/" + std::to_string(i);
        const auto* xyz = (*points->if_array())[i].if_array();
        if (!xyz || xyz->size() != 3 || !(*xyz)[0].if_number() || !(*xyz)[1].if_number() ||
            !(*xyz)[2].if_number())
          fail(at, "a point is three numbers in mm");
        const math::Vec3 p{*(*xyz)[0].if_number(), *(*xyz)[1].if_number(), *(*xyz)[2].if_number()};
        std::vector<int> near;
        for (const auto& face : faces) {
          auto d = kernel.face_distance(face, p);
          if (!d) fail(at, d.error().message);
          if (*d <= tolerance) {
            auto n = face_number(face);
            if (!n) fail(at, n.error().message);
            near.push_back(*n);
          }
        }
        if (near.empty()) fail(at, "the point lies on no face of the part");
        if (near.size() > 1) fail(at, "the point lies on several faces: pick one inside a face");
        if (std::ranges::find(numbers, near.front()) == numbers.end())
          numbers.push_back(near.front());
      }
      return numbers;
    };
    const auto prepared = prepare(study, resolve, "");
    const LengthReader length = [](const Value& v, const std::string& p) {
      return value(v, Dimension::kLength, p);
    };
    const auto& mesh = required(*study.find("mesh"), "size", "/mesh");
    auto result =
        solve(study, prepared, shape, sizes(options, length(mesh, "/mesh/size"), "/mesh/size"),
              length, "", options.field);
    result.push_back({"provenance", Object{{"step_sha256", ir::sha256(step).hex()},
                                           {"mesher", mesher_identity()},
                                           {"solver", solver_identity()}}});
    return result;
  } catch (Failure& f) {
    return std::unexpected(errors_of(f));
  }
}
}  // namespace nf::study
