// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "evaluator.hpp"
#include "part_fixture.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numbers>
#include <set>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf;
using namespace nf::kernel_service;
using Catch::Matchers::WithinAbs;

namespace {
void display_mesh(kernel::IKernel& kernel, const kernel::ShapePtr& shape,
                  const kernel::Mesh& mesh) {
  REQUIRE_FALSE(mesh.vertices.empty());
  REQUIRE_FALSE(mesh.indices.empty());
  CHECK(mesh.indices.size() % 3 == 0);
  CHECK(mesh.faces.size() == shape->measures().faces);
  const auto descriptions = kernel.faces(shape).value();
  std::size_t count = 0;
  for (const auto& face : mesh.faces) {
    REQUIRE(face.face);
    CHECK(face.face->owner() == shape);
    CHECK(face.first_index == count);
    CHECK(face.index_count > 0);
    CHECK(face.index_count % 3 == 0);
    CHECK(std::ranges::any_of(descriptions,
                              [&](const auto& d) { return d.entity->same_as(*face.face); }));
    count += face.index_count;
  }
  CHECK(count == mesh.indices.size());
  double volume = 0, area = 0;
  bool finite = true, outward = true, unit = true;
  const auto origin = mesh.vertices.front().position;
  for (const auto& v : mesh.vertices) {
    for (const double x :
         {v.position.x, v.position.y, v.position.z, v.normal.x, v.normal.y, v.normal.z})
      finite = finite && std::isfinite(x);
    unit = unit && std::abs(math::length(v.normal) - 1) <= 1e-6;
  }
  for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
    for (std::size_t j = 0; j < 3; ++j) REQUIRE(mesh.indices[i + j] < mesh.vertices.size());
    const auto& a = mesh.vertices[mesh.indices[i]];
    const auto& b = mesh.vertices[mesh.indices[i + 1]];
    const auto& c = mesh.vertices[mesh.indices[i + 2]];
    const auto cross = math::cross(b.position - a.position, c.position - a.position);
    outward = outward && math::dot(cross, a.normal + b.normal + c.normal) > 0;
    area += math::length(cross) / 2;
    volume +=
        math::dot(a.position - origin, math::cross(b.position - origin, c.position - origin)) / 6;
  }
  CHECK(finite);
  CHECK(unit);
  CHECK(outward);
  CHECK_THAT(volume, WithinAbs(shape->measures().volume, shape->measures().volume * 0.01));
  CHECK_THAT(area, WithinAbs(shape->measures().area, shape->measures().area * 0.01));
}
}  // namespace

TEST_CASE("all twenty current reference parts produce complete indexed viewport meshes",
          "[mesh-evaluator]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  std::set<std::string> names;
  for (const auto& entry : std::filesystem::directory_iterator(NF_REFERENCE_PARTS_DIR)) {
    if (entry.path().extension() != ".nightfall") continue;
    CAPTURE(entry.path().filename().string());
    names.insert(entry.path().stem().string());
    std::ifstream stream(entry.path());
    const std::string text{std::istreambuf_iterator<char>{stream}, {}};
    const auto result = evaluator.evaluate(text).value();
    REQUIRE(result.ok());
    const auto& body = result.bodies.at("part");
    for (const auto& options : {kernel::MeshingOptions{}, kernel::MeshingOptions{0.02, 0.15}}) {
      CAPTURE(options.linear_deflection, options.angular_deflection);
      const auto mesh = kernel->tessellate(body.shape, options);
      if (!mesh) UNSCOPED_INFO(mesh.error().message);
      REQUIRE(mesh);
      display_mesh(*kernel, body.shape, *mesh);
    }
    const auto warm = evaluator.evaluate(text).value();
    REQUIRE(warm.ok());
    CHECK(warm.cache_hits == result.features.size());
    CHECK(warm.bodies.at("part").shape == body.shape);
    CHECK(warm.bodies.at("part").hash == body.hash);
  }
  CHECK(names.size() == 20);
}

TEST_CASE("Eclipse forearm document keeps complete patterned loft meshes", "[forearm-mesh]") {
  std::ifstream stream(std::filesystem::path(NF_KERNEL_FIXTURES_DIR) / "eclipse_forearm.nightfall");
  REQUIRE(stream.good());
  const std::string source{std::istreambuf_iterator<char>{stream}, {}};
  const auto document = ir::read_document(source).value();
  REQUIRE(document.features.size() == 5);
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {96, 64 * 1024 * 1024});
  const auto result = evaluator.evaluate(source).value();
  REQUIRE(result.ok());
  const auto shape = result.bodies.at("forearm").shape;
  CHECK(shape->measures().solids == 4);
  CHECK_THAT(shape->measures().volume, WithinAbs(4 * 62848 * std::numbers::pi, 1e-6));
  const auto mesh = kernel->tessellate(shape, {});
  INFO((mesh ? "success" : mesh.error().message));
  REQUIRE(mesh);
  display_mesh(*kernel, shape, *mesh);
}

TEST_CASE("chamfered mirrored parts keep mesh picking handles across cache reuse and eviction",
          "[mesh-evaluator]") {
  auto d = test::part(2);
  d.features.push_back(test::feature("bevel", "chamfer", R"({"distance":1,
    "edges":{"edges_of":{"created_by":"base","role":"cap_end","expect":"one"}}})"));
  d.features.push_back(test::part(3).features.back());
  d.features.push_back(
      test::feature("reflect", "mirror", R"({"bodies":["plate"],"plane":{"datum":"yz"}})"));
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  const auto text = test::source(d);
  const auto first = evaluator.evaluate(text).value();
  REQUIRE(first.ok());
  const auto original = first.bodies.at("plate").shape;
  const auto mesh = kernel->tessellate(original, {}).value();
  display_mesh(*kernel, original, mesh);
  CHECK(original->measures().solids == 2);
  CHECK_THAT(original->measures().volume, WithinAbs(2 * (6000 - 86. / 3), 1e-6));
  const auto warm = evaluator.evaluate(text).value();
  REQUIRE(warm.ok());
  CHECK(warm.cache_hits == d.features.size());
  CHECK(warm.bodies.at("plate").shape == original);
  evaluator.clear_cache();
  const auto recreated = evaluator.evaluate(text).value();
  REQUIRE(recreated.ok());
  CHECK(recreated.bodies.at("plate").shape != original);
  display_mesh(*kernel, original, mesh);
  for (const auto& face : mesh.faces) CHECK(kernel->edges(face.face));
}
