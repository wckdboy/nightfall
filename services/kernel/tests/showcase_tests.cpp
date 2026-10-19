// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "evaluator.hpp"

#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <STEPControl_Reader.hxx>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <numbers>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf;
using namespace nf::kernel_service;
using Catch::Matchers::WithinAbs;
namespace {
constexpr double pi = std::numbers::pi;
ir::Document showcase() {
  const auto path = std::filesystem::path(NF_REFERENCE_PARTS_DIR).parent_path() / "modeling" /
                    "asterion.nightfall";
  std::ifstream file(path);
  const std::string text{std::istreambuf_iterator<char>{file}, {}};
  return ir::read_document(text).value();
}
Evaluation build(Evaluator& evaluator, const ir::Document& document) {
  auto result = evaluator.evaluate(ir::write_document(document).value()).value();
  for (const auto& f : result.features)
    for (const auto& e : f.errors) UNSCOPED_INFO(e.pointer << ": " << e.message);
  REQUIRE(result.ok());
  return result;
}
double verify(const Evaluation& result, double count = 64, double height = 28, double radius = 0.5,
              double modules = 1) {
  const double fin_section = 48 - (4 - pi) * radius * radius;
  const double fin_perimeter = 82.4 - 8 * radius + 2 * pi * radius;
  // Independent primitive/frustum integration; no measured values as mass oracles.
  const std::map<std::string, std::pair<double, double>> expected{
      {"mount", {169344 - 120 * pi, 59952 + 416 * pi}},
      {"spreader", {18096 * pi, 9672 * pi}},
      {"hub", {(35446. / 3) * pi, (1626 + 40 * std::sqrt(712.) + 30 * std::sqrt(32.)) * pi}},
      {"fins", {count * fin_section * height, count * (2 * fin_section + fin_perimeter * height)}},
      {"stands", {640 * pi, 800 * pi}},
      {"fasteners", {310 * pi, (400 + 40 * std::sqrt(2.)) * pi}},
      {"guard", {2412 * pi, 2412 * pi}},
      {"ties", {261 * pi, 366 * pi}}};
  double total = 0;
  std::size_t live = 0;
  for (const auto& [id, body] : result.bodies) {
    if (!body.shape) continue;
    CAPTURE(id);
    ++live;
    const auto& [volume, area] = expected.at(id);
    const auto& m = body.shape->measures();
    CHECK_THAT(m.volume, WithinAbs(modules * volume, 1e-5));
    CHECK_THAT(m.area, WithinAbs(modules * area, 1e-5));
    CHECK_THAT(m.centroid.x, WithinAbs(100 * (modules - 1), 1e-7));
    CHECK_THAT(m.centroid.y, WithinAbs(0, 1e-7));
    for (const auto& name : body.names) CHECK(name.entity->owner() == body.shape);
    total += volume * modules;
  }
  CHECK(live == 8);
  return total;
}
}  // namespace

TEST_CASE("Asterion showcase has independently verified exact mass through edits and cache reuse",
          "[showcase]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {96, 64 * 1024 * 1024});
  auto d = showcase();
  const auto original = build(evaluator, d);
  verify(original);
  const auto warm = build(evaluator, d);
  verify(warm);
  CHECK(warm.cache_hits == 28);
  CHECK(warm.kernel_calls == 13);  // current face/edge selection queries still execute
  d.params[0].value = 32.;
  d.params[1].value = "18 mm";
  d.params[2].value = "0.25 mm";
  verify(build(evaluator, d), 32, 18, 0.25);
  d.params[3].value = 2.;
  verify(build(evaluator, d), 32, 18, 0.25, 2);
  evaluator.clear_cache();
  verify(original);  // retained readers survive cache clearing and different parameter sets
}

TEST_CASE("Asterion exact STEP and indexed meshes cover all eighty solids", "[showcase]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {96, 64 * 1024 * 1024});
  const auto result = build(evaluator, showcase());
  const double expected_volume = verify(result);
  std::vector<kernel::ShapePtr> shapes;
  std::size_t solids = 0, faces = 0, triangles = 0;
  double mesh_volume = 0, exact_volume = 0;
  for (const auto& [id, body] : result.bodies) {
    if (!body.shape) continue;
    shapes.push_back(body.shape);
    const auto& m = body.shape->measures();
    exact_volume += m.volume;
    solids += m.solids;
    faces += m.faces;
    const auto mesh = kernel->tessellate(body.shape, {}).value();
    CHECK(mesh.faces.size() == m.faces);
    triangles += mesh.indices.size() / 3;
    const auto origin = mesh.vertices.front().position;
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
      const auto a = mesh.vertices.at(mesh.indices[i]).position - origin;
      const auto b = mesh.vertices.at(mesh.indices[i + 1]).position - origin;
      const auto c = mesh.vertices.at(mesh.indices[i + 2]).position - origin;
      mesh_volume += math::dot(a, math::cross(b, c)) / 6;
    }
    for (const auto& face : mesh.faces) CHECK(face.face->owner() == body.shape);
  }
  CHECK(solids == 80);
  CHECK(faces == 745);
  CHECK(triangles > 20000);
  CHECK_THAT(exact_volume, WithinAbs(expected_volume, 1e-5));
  CHECK(std::abs(mesh_volume / expected_volume - 1) < 0.01);
  auto encoded = kernel->export_step(shapes).value();
  std::istringstream stream(encoded);
  STEPControl_Reader reader;
  REQUIRE(reader.ReadStream("asterion.step", stream) == IFSelect_RetDone);
  REQUIRE(reader.TransferRoots() > 0);
  const auto loaded = reader.OneShape();
  CHECK(BRepCheck_Analyzer(loaded).IsValid());
  GProp_GProps properties;
  BRepGProp::VolumeProperties(loaded, properties);
  CHECK_THAT(properties.Mass(), WithinAbs(expected_volume, 1e-5));
}
