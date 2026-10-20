// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "evaluator.hpp"
#include "snapshot.hpp"

#include <STEPControl_Reader.hxx>

#include <array>
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
ir::Document document() {
  std::ifstream file(std::filesystem::path(NF_REFERENCE_PARTS_DIR).parent_path() / "modeling" /
                     "nocturne.nightfall");
  REQUIRE(file);
  return ir::read_document(std::string{std::istreambuf_iterator<char>{file}, {}}).value();
}
Evaluation build(Evaluator& evaluator, const ir::Document& part) {
  auto result = evaluator.evaluate(ir::write_document(part).value()).value();
  for (const auto& f : result.features)
    for (const auto& e : f.errors) UNSCOPED_INFO(e.pointer << ": " << e.message);
  REQUIRE(result.ok());
  return result;
}
double loft_volume(double length, std::array<double, 3> a, std::array<double, 3> b) {
  // Integrate the product of independent quadratic interpolants through t = 0, 1/2, 1.
  auto polynomial = [](auto r) {
    return std::array{r[0], -3 * r[0] + 4 * r[1] - r[2], 2 * r[0] - 4 * r[1] + 2 * r[2]};
  };
  a = polynomial(a);
  b = polynomial(b);
  double section = 0;
  for (std::size_t i = 0; i < 3; ++i)
    for (std::size_t j = 0; j < 3; ++j) section += a[i] * b[j] / static_cast<double>(i + j + 1);
  return pi * length * section;
}
double cable_length(int steps) {
  // Positive composite Simpson quadrature of the authored Bezier derivative.
  double sum = 0;
  for (int i = 0; i <= steps; ++i) {
    const double t = static_cast<double>(i) / steps, s = 1 - t;
    const double x = 6 * s * t * 210 + 3 * t * t * 100;
    const double y = -3 * s * s * 170 - 6 * s * t * 85 + 3 * t * t * 165;
    sum += (i == 0 || i == steps ? 1 : (i % 2 ? 4 : 2)) * std::hypot(x, y);
  }
  return sum / (3 * steps);
}
double verify(const Evaluation& evaluation, double waist = 1) {
  auto ring = [](double r, double bore, double height, int holes = 0, int copies = 1) {
    return copies * pi * (r * r - bore * bore - holes * 2.75 * 2.75) * height;
  };
  auto rounded = [](double width, double depth, double radius) {
    return width * depth - (4 - pi) * radius * radius;
  };
  const double cable = cable_length(4096);
  CHECK_THAT(cable, WithinAbs(cable_length(8192), 1e-8));
  const std::map<std::string, double> volumes{
      {"pedestal", 196404 * pi},
      {"cooling_fins", 32 * 14 * rounded(16, 2, 0.6)},
      {"shoulder_housing", ring(42, 15, 110)},
      {"shoulder_covers", ring(46, 15, 8, 8, 2)},
      {"shoulder_fasteners", 16 * pi * (16 - 1.8 * 1.8) * 3},
      {"elbow_housing", ring(34, 11, 96)},
      {"elbow_covers", ring(38, 11, 8, 8, 2)},
      {"elbow_fasteners", 16 * pi * (16 - 1.8 * 1.8) * 3},
      {"wrist_housing", ring(26, 10, 70)},
      {"wrist_covers", ring(30, 10, 8, 8, 2)},
      {"wrist_fasteners", 16 * pi * (16 - 1.8 * 1.8) * 3},
      {"upper_links", 2 * loft_volume(190, {22, 16 * waist, 19}, {12, 9 * waist, 11})},
      {"forearm", loft_volume(160, {24, 18 * waist, 16}, {27, 20 * waist, 17})},
      {"cable_harness", 2 * pi * 3.5 * 3.5 * cable},
      {"harness_base_anchor", rounded(18, 30, 3) * 16},
      {"harness_wrist_anchor", rounded(18, 46, 3) * 36},
      {"tool_neck", ring(16, 9, 18)},
      {"tool_flange", ring(28, 9, 6)},
      {"gripper_palm", (rounded(80, 44, 6) - 4 * pi * 2.75 * 2.75) * 16},
      {"gripper_jaws", 2 * (rounded(12, 28, 3) * 56 + 18 * 28 * 16)},
      {"palm_fasteners", 4 * pi * 16 * 3}};
  double total = 0;
  std::size_t live = 0, solids = 0;
  for (const auto& [id, body] : evaluation.bodies) {
    if (!body.shape) continue;
    CAPTURE(id);
    ++live;
    const auto expected = volumes.at(id);
    const auto& m = body.shape->measures();
    CHECK_THAT(m.volume, WithinAbs(expected, id == "cable_harness" ? 1e-3 : 1e-5));
    CHECK(std::isfinite(m.area));
    CHECK(m.area > 0);
    solids += m.solids;
    for (const auto& name : body.names) CHECK(name.entity->owner() == body.shape);
    total += expected;
  }
  CHECK(live == 21);
  CHECK(solids == 106);
  return total;
}
}  // namespace

TEST_CASE("Nocturne component volumes survive edits cache reuse and exact STEP reload",
          "[nocturne]") {
  auto backend = kernel::make_occt_kernel().value();
  Evaluator evaluator(*backend, {96, 64 * 1024 * 1024});
  auto part = document();
  REQUIRE(part.features.size() == 71);
  const auto original = build(evaluator, part);
  const double expected = verify(original);
  const auto warm = build(evaluator, part);
  verify(warm);
  CHECK(warm.cache_hits == 71);
  part.params[3].value = "34 mm";
  part.params[4].value = 0.95;
  verify(build(evaluator, part), 0.95);
  evaluator.clear_cache();
  verify(original);

  std::vector<kernel::ShapePtr> shapes;
  double mesh_volume = 0;
  std::size_t triangles = 0;
  for (const auto& [id, body] : original.bodies) {
    if (!body.shape) continue;
    shapes.push_back(body.shape);
    const auto mesh = backend->tessellate(body.shape, {}).value();
    CHECK(mesh.faces.size() == body.shape->measures().faces);
    triangles += mesh.indices.size() / 3;
    const auto origin = mesh.vertices.front().position;
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
      const auto a = mesh.vertices.at(mesh.indices[i]).position - origin;
      const auto b = mesh.vertices.at(mesh.indices[i + 1]).position - origin;
      const auto c = mesh.vertices.at(mesh.indices[i + 2]).position - origin;
      mesh_volume += math::dot(a, math::cross(b, c)) / 6;
    }
  }
  CHECK(triangles > 200000);
  CHECK(triangles < 1000000);
  CHECK(std::abs(mesh_volume / expected - 1) < 0.01);
  const auto step = backend->export_step(shapes).value();
  std::istringstream stream(step);
  STEPControl_Reader reader;
  REQUIRE(reader.ReadStream("nocturne.step", stream) == IFSelect_RetDone);
  REQUIRE(reader.TransferRoots() > 0);
  const auto loaded = kernel::detail::snapshot(reader.OneShape());
  REQUIRE(loaded);
  CHECK((*loaded)->measures().solids == 106);
  CHECK_THAT((*loaded)->measures().volume, WithinAbs(expected, 1e-3));
}
