// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "nf/kernel/kernel.hpp"
#include "nf/math/constructors.hpp"
#include "snapshot.hpp"

#include <BRepBuilderAPI_Copy.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <limits>
#include <numbers>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf::kernel;
using namespace nf::math;
using Catch::Matchers::WithinAbs;

namespace {
constexpr double kPi = std::numbers::pi;
OperationResult box(IKernel& kernel, Vec3 origin = {}, Vec3 size = {10, 20, 30}) {
  std::vector<std::vector<NurbsCurve2>> paths{
      {make_line(Vec2{}, {size.x, 0}), make_line(Vec2{size.x, 0}, {size.x, size.y}),
       make_line(Vec2{size.x, size.y}, {0, size.y}), make_line(Vec2{0, size.y}, {})}};
  return kernel.extrude({{origin}, regions(paths, FillRule::kEvenOdd).value(), {0, 0, size.z}})
      .value();
}
std::string serialized(const ShapePtr& shape) {
  std::ostringstream stream;
  BRepTools::Write(std::dynamic_pointer_cast<const detail::Snapshot>(shape)->geometry, stream);
  return stream.str();
}
void unmeshed(const ShapePtr& shape) {
  const auto source = std::dynamic_pointer_cast<const detail::Snapshot>(shape);
  for (TopExp_Explorer e(source->geometry, TopAbs_FACE); e.More(); e.Next()) {
    TopLoc_Location location;
    CHECK(BRep_Tool::Triangulation(TopoDS::Face(e.Current()), location).IsNull());
  }
}
struct Approximation {
  double volume = 0, area = 0;
};
Approximation inspect(IKernel& kernel, const ShapePtr& source, const Mesh& mesh) {
  REQUIRE_FALSE(mesh.vertices.empty());
  REQUIRE_FALSE(mesh.indices.empty());
  REQUIRE(mesh.indices.size() % 3 == 0);
  REQUIRE(mesh.faces.size() == source->measures().faces);
  const auto faces = kernel.faces(source).value();
  std::size_t offset = 0;
  for (const auto& range : mesh.faces) {
    REQUIRE(range.face);
    CHECK(range.face->owner() == source);
    CHECK(range.face->kind() == EntityKind::kFace);
    CHECK(std::ranges::count_if(
              faces, [&](const auto& f) { return f.entity->same_as(*range.face); }) == 1);
    CHECK(std::ranges::count_if(mesh.faces,
                                [&](const auto& f) { return f.face->same_as(*range.face); }) == 1);
    CHECK(range.first_index == offset);
    REQUIRE(range.index_count > 0);
    CHECK(range.index_count % 3 == 0);
    offset += range.index_count;
    REQUIRE(offset <= mesh.indices.size());
  }
  CHECK(offset == mesh.indices.size());
  for (const auto& vertex : mesh.vertices) {
    const auto p = vertex.position, n = vertex.normal;
    for (const double v : {p.x, p.y, p.z, n.x, n.y, n.z}) CHECK(std::isfinite(v));
    CHECK_THAT(length(n), WithinAbs(1, 1e-6));
    const auto& m = source->measures();
    CHECK(p.x >= m.minimum.x - 1e-6);
    CHECK(p.x <= m.maximum.x + 1e-6);
    CHECK(p.y >= m.minimum.y - 1e-6);
    CHECK(p.y <= m.maximum.y + 1e-6);
    CHECK(p.z >= m.minimum.z - 1e-6);
    CHECK(p.z <= m.maximum.z + 1e-6);
  }
  Approximation result;
  const auto origin = mesh.vertices.front().position;
  for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
    for (std::size_t j = 0; j < 3; ++j) REQUIRE(mesh.indices[i + j] < mesh.vertices.size());
    const auto& a = mesh.vertices[mesh.indices[i]];
    const auto& b = mesh.vertices[mesh.indices[i + 1]];
    const auto& c = mesh.vertices[mesh.indices[i + 2]];
    const auto cross_product = cross(b.position - a.position, c.position - a.position);
    INFO("triangle " << i / 3 << " area2 " << length(cross_product) << " ab "
                     << length(b.position - a.position) << " ac " << length(c.position - a.position)
                     << " bc " << length(c.position - b.position));
    CHECK(length(cross_product) > 0);
    CHECK(dot(cross_product, a.normal + b.normal + c.normal) > 0);
    result.area += length(cross_product) / 2;
    result.volume += dot(a.position - origin, cross(b.position - origin, c.position - origin)) / 6;
  }
  return result;
}
}  // namespace

TEST_CASE("indexed box meshes cover original faces with outward normals and exact winding",
          "[mesh-kernel]") {
  auto kernel = make_occt_kernel().value();
  const auto source = box(*kernel).shape;
  const auto saved = serialized(source);
  unmeshed(source);
  const auto mesh = kernel->tessellate(source, {});
  REQUIRE(mesh);
  CHECK(mesh->vertices.size() == 24);
  CHECK(mesh->indices.size() == 36);
  const auto approximate = inspect(*kernel, source, *mesh);
  CHECK_THAT(approximate.volume, WithinAbs(6000, 1e-7));
  CHECK_THAT(approximate.area, WithinAbs(2200, 1e-7));
  for (const auto& range : mesh->faces) {
    const auto plane = kernel->face_plane(range.face).value();
    for (std::size_t i = range.first_index; i < range.first_index + range.index_count; ++i)
      CHECK_THAT(dot(mesh->vertices[mesh->indices[i]].normal, plane.normal), WithinAbs(1, 1e-6));
  }
  unmeshed(source);
  CHECK(serialized(source) == saved);
}

TEST_CASE("mesh placement and reflection preserve outward triangle winding", "[mesh-kernel]") {
  auto kernel = make_occt_kernel().value();
  const auto source = box(*kernel).shape;
  const auto placed = kernel->transform(source, {{3, 4, 5}, {}, {0, 1, 0}, kPi / 2}).value().shape;
  const auto mirrored = kernel->mirror(placed, {{2, 0, 0}, {0, 1, 0}, {1, 0, 0}}).value().shape;
  for (const auto& shape : {placed, mirrored}) {
    const auto saved = serialized(shape);
    const auto mesh = kernel->tessellate(shape, {}).value();
    const auto approximate = inspect(*kernel, shape, mesh);
    CHECK_THAT(approximate.volume, WithinAbs(6000, 1e-7));
    CHECK_THAT(approximate.area, WithinAbs(2200, 1e-7));
    unmeshed(shape);
    CHECK(serialized(shape) == saved);
  }
  unmeshed(source);
}

TEST_CASE("annular meshes converge with exact cylinder normals and inward bore winding",
          "[mesh-kernel]") {
  auto kernel = make_occt_kernel().value();
  std::vector<std::vector<NurbsCurve2>> paths{{make_circle(Vec2{}, 10)}, {make_circle(Vec2{}, 5)}};
  const auto source =
      kernel->extrude({{}, regions(paths, FillRule::kEvenOdd).value(), {0, 0, 30}}).value().shape;
  const auto saved = serialized(source);
  const auto coarse = kernel->tessellate(source, {0.8, 0.6}).value();
  const auto fine = kernel->tessellate(source, {0.02, 0.15}).value();
  const auto c = inspect(*kernel, source, coarse), refined = inspect(*kernel, source, fine);
  CHECK(fine.indices.size() > coarse.indices.size());
  CHECK(std::abs(refined.volume - 2250 * kPi) < std::abs(c.volume - 2250 * kPi));
  CHECK_THAT(refined.volume, WithinAbs(2250 * kPi, 2250 * kPi * 0.002));
  CHECK_THAT(refined.area, WithinAbs(1050 * kPi, 1050 * kPi * 0.002));
  const auto faces = kernel->faces(source).value();
  for (const auto& range : fine.faces) {
    const auto& face =
        *std::ranges::find_if(faces, [&](const auto& f) { return f.entity->same_as(*range.face); });
    if (!face.cylinder) continue;
    const auto sign = face.cylinder->radius > 7 ? 1. : -1.;
    for (std::size_t i = range.first_index; i < range.first_index + range.index_count; ++i) {
      const auto& v = fine.vertices[fine.indices[i]];
      const auto radial = normalized(Vec3{v.position.x, v.position.y, 0}).value();
      CHECK_THAT(dot(v.normal, radial), WithinAbs(sign, 1e-6));
    }
  }
  CHECK(serialized(source) == saved);
  unmeshed(source);
}

TEST_CASE("spherical meshes cover periodic seams and poles with finite outward normals",
          "[mesh-kernel]") {
  auto kernel = make_occt_kernel().value();
  const auto source =
      detail::snapshot(BRepPrimAPI_MakeSphere(gp_Pnt(30, 0, 0), 10).Shape()).value();
  const auto mesh = kernel->tessellate(source, {0.02, 0.15}).value();
  const auto approximate = inspect(*kernel, source, mesh);
  CHECK_THAT(approximate.volume, WithinAbs(4000 * kPi / 3, 4000 * kPi / 3 * 0.005));
  CHECK_THAT(approximate.area, WithinAbs(400 * kPi, 400 * kPi * 0.005));
  for (const auto& vertex : mesh.vertices)
    CHECK_THAT(dot(vertex.normal, normalized(vertex.position - Vec3{30, 0, 0}).value()),
               WithinAbs(1, 1e-6));
  unmeshed(source);
}

TEST_CASE("mesh queries preserve enclosed cavities and multiple solid components",
          "[mesh-kernel]") {
  auto kernel = make_occt_kernel().value();
  const auto source = box(*kernel).shape;
  const auto cavity = kernel
                          ->boolean_operation(source, {box(*kernel, {1, 2, 3}, {2, 3, 4}).shape},
                                              BooleanOp::kSubtract)
                          .value()
                          .shape;
  const auto separate =
      kernel->boolean_operation(source, {box(*kernel, {50, 0, 0}).shape}, BooleanOp::kUnion)
          .value()
          .shape;
  for (const auto& shape : {cavity, separate}) {
    const auto saved = serialized(shape);
    const auto mesh = kernel->tessellate(shape, {}).value();
    const auto approximate = inspect(*kernel, shape, mesh);
    CHECK_THAT(approximate.volume, WithinAbs(shape->measures().volume, 1e-7));
    CHECK_THAT(approximate.area, WithinAbs(shape->measures().area, 1e-7));
    CHECK(serialized(shape) == saved);
    unmeshed(shape);
  }
  CHECK_THAT(cavity->measures().volume, WithinAbs(5976, 1e-7));
  CHECK_THAT(cavity->measures().area, WithinAbs(2252, 1e-7));
  CHECK(separate->measures().solids == 2);
}

TEST_CASE("tessellation does not replace existing triangulations or change later precision",
          "[mesh-kernel]") {
  auto kernel = make_occt_kernel().value();
  BRepBuilderAPI_Copy copy(BRepPrimAPI_MakeSphere(10).Shape(), true, false);
  BRepMesh_IncrementalMesh triangulate(copy.Shape(), 0.5, false, 0.5, false);
  const auto source = detail::snapshot(copy.Shape()).value();
  const auto saved = serialized(source);
  const auto fine = kernel->tessellate(source, {0.02, 0.15});
  REQUIRE(fine);
  CHECK(serialized(source) == saved);
  const auto coarse = kernel->tessellate(source, {0.8, 0.6});
  REQUIRE(coarse);
  CHECK(fine->indices.size() > coarse->indices.size());
  CHECK(serialized(source) == saved);
}

TEST_CASE("mesh queries reject invalid inputs and enforce bounded returned geometry",
          "[mesh-kernel]") {
  auto kernel = make_occt_kernel().value();
  const auto source = box(*kernel).shape;
  const auto saved = serialized(source);
  CHECK_FALSE(kernel->tessellate({}, {}));
  struct Foreign final : Shape {
    Measures m;
    const Measures& measures() const noexcept override { return m; }
    std::size_t estimated_bytes() const noexcept override { return 0; }
  };
  CHECK_FALSE(kernel->tessellate(std::make_shared<Foreign>(), {}));
  const double inf = std::numeric_limits<double>::infinity();
  const double nan = std::numeric_limits<double>::quiet_NaN();
  for (const auto& options :
       {MeshingOptions{0, 0.3}, MeshingOptions{-1, 0.3}, MeshingOptions{1e-8, 0.3},
        MeshingOptions{inf, 0.3}, MeshingOptions{nan, 0.3}, MeshingOptions{0.1, 0},
        MeshingOptions{0.1, -1}, MeshingOptions{0.1, inf}, MeshingOptions{0.1, nan},
        MeshingOptions{0.1, 4}, MeshingOptions{0.1, 0.3, 0}, MeshingOptions{0.1, 0.3, 1'000'001},
        MeshingOptions{0.1, 0.3, 11}}) {
    const auto failed = kernel->tessellate(source, options);
    REQUIRE_FALSE(failed);
    CHECK(failed.error().code == "kernel");
    CHECK(serialized(source) == saved);
  }
  const auto exactly = kernel->tessellate(source, {0.1, 0.3, 12});
  REQUIRE(exactly);
  CHECK(exactly->indices.size() == 36);
  unmeshed(source);
}

TEST_CASE("mesh face associations retain original geometry after facade destruction",
          "[mesh-kernel]") {
  Mesh retained;
  ShapePtr source;
  {
    auto kernel = make_occt_kernel().value();
    source = box(*kernel).shape;
    retained = kernel->tessellate(source, {}).value();
  }
  auto kernel = make_occt_kernel().value();
  const auto approximate = inspect(*kernel, source, retained);
  CHECK_THAT(approximate.volume, WithinAbs(6000, 1e-7));
  for (const auto& range : retained.faces) CHECK(kernel->edges(range.face));
}

TEST_CASE("repeated compound meshing preserves curved seams cavities and original face bindings",
          "[mesh-kernel]") {
  auto kernel = make_occt_kernel().value();
  const std::vector<std::vector<NurbsCurve2>> paths{{make_circle(Vec2{}, 3)},
                                                    {make_circle(Vec2{}, 2)}};
  const auto sleeve =
      kernel->extrude({{}, regions(paths, FillRule::kEvenOdd).value(), {0, 0, 6}}).value().shape;
  const auto cavity =
      kernel
          ->boolean_operation(box(*kernel, {50, 0, 0}).shape,
                              {box(*kernel, {51, 2, 3}, {2, 3, 4}).shape}, BooleanOp::kSubtract)
          .value()
          .shape;
  std::vector<ShapePtr> tools{
      cavity, detail::snapshot(BRepPrimAPI_MakeSphere(gp_Pnt(-10, 0, 0), 3).Shape()).value()};
  for (int i = 1; i < 8; ++i)
    tools.push_back(
        kernel->transform(sleeve, {{0, i * 10., 0}, {}, {1, 0, 0}, i * kPi / 8}).value().shape);
  const auto compound = kernel->boolean_operation(sleeve, tools, BooleanOp::kUnion).value().shape;
  const auto reflected = kernel->mirror(compound, {{}, {0, 1, 0}, {1, 0, 0}}).value().shape;
  for (const auto& source : {compound, reflected}) {
    REQUIRE(source->measures().solids == 10);
    const auto saved = serialized(source);
    const auto baseline = kernel->tessellate(source, {0.05, 0.2}).value();
    const auto a = inspect(*kernel, source, baseline);
    CHECK_THAT(a.volume, WithinAbs(source->measures().volume, source->measures().volume * 0.002));
    CHECK_THAT(a.area, WithinAbs(source->measures().area, source->measures().area * 0.002));
    for (int pass = 0; pass < 3; ++pass) {
      const auto repeated = kernel->tessellate(source, {0.05, 0.2}).value();
      REQUIRE(repeated.vertices.size() == baseline.vertices.size());
      REQUIRE(repeated.indices == baseline.indices);
      REQUIRE(repeated.faces.size() == baseline.faces.size());
      for (std::size_t i = 0; i < repeated.vertices.size(); ++i) {
        CHECK(repeated.vertices[i].position == baseline.vertices[i].position);
        CHECK(repeated.vertices[i].normal == baseline.vertices[i].normal);
      }
      for (std::size_t i = 0; i < repeated.faces.size(); ++i) {
        CHECK(repeated.faces[i].face->same_as(*baseline.faces[i].face));
        CHECK(repeated.faces[i].first_index == baseline.faces[i].first_index);
        CHECK(repeated.faces[i].index_count == baseline.faces[i].index_count);
      }
    }
    CHECK_FALSE(kernel->tessellate(source, {0.05, 0.2, baseline.indices.size() / 3 - 1}));
    CHECK(serialized(source) == saved);
    unmeshed(source);
  }
  CHECK_THAT(sleeve->measures().volume, WithinAbs(30 * kPi, 1e-7));
  CHECK_THAT(cavity->measures().volume, WithinAbs(5976, 1e-7));
  unmeshed(sleeve);
  unmeshed(cavity);
}
