// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "nf/kernel/kernel.hpp"
#include "nf/math/constructors.hpp"
#include "snapshot.hpp"

#include <BRepTools.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Reader.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <numbers>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf::kernel;
using namespace nf::math;
using Catch::Matchers::WithinAbs;
namespace {
constexpr double kPi = std::numbers::pi;
Region polygon(const std::vector<Vec2>& points) {
  Region result;
  for (std::size_t i = 0; i < points.size(); ++i)
    result.outer.curves.push_back(make_line(points[i], points[(i + 1) % points.size()]));
  return result;
}
Region rectangle(double scale = 1) {
  return polygon({{-5 * scale, -4 * scale},
                  {5 * scale, -4 * scale},
                  {5 * scale, 4 * scale},
                  {-5 * scale, 4 * scale}});
}
Loft frustum() { return {{{{}, rectangle()}, {{{0, 0, 30}}, rectangle(2)}}}; }
OperationResult build(IKernel& kernel, const Loft& input) {
  auto result = kernel.loft(input);
  INFO((result ? "success" : result.error().message));
  REQUIRE(result);
  return *result;
}
void check_provenance(IKernel& kernel, const OperationResult& result, const Loft& input) {
  const auto output = std::dynamic_pointer_cast<const detail::Snapshot>(result.shape);
  TopTools_IndexedMapOfShape faces, covered;
  TopExp::MapShapes(output->geometry, TopAbs_FACE, faces);
  std::map<std::pair<std::size_t, std::size_t>, std::vector<EntityPtr>> sources;
  std::size_t caps = 0;
  for (const auto& role : result.generated) {
    REQUIRE(role.entity);
    CHECK(role.entity->owner() == result.shape);
    CHECK(role.entity->kind() == EntityKind::kFace);
    const auto& geometry = static_cast<const detail::Subshape&>(*role.entity).geometry;
    REQUIRE(faces.Contains(geometry));
    CHECK(geometry.Orientation() == faces(faces.FindIndex(geometry)).Orientation());
    covered.Add(geometry);
    if (role.role == "side") {
      REQUIRE(role.from);
      CHECK(role.from->region == 0);
      CHECK(role.from->loop == 0);
      REQUIRE(role.from->section < input.sections.size());
      REQUIRE(role.from->curve < input.sections[role.from->section].region.outer.curves.size());
      sources[{role.from->section, role.from->curve}].push_back(role.entity);
    } else {
      CHECK((role.role == "cap_start" || role.role == "cap_end"));
      CHECK_FALSE(role.from);
      const auto& section =
          role.role == "cap_start" ? input.sections.front() : input.sections.back();
      const auto& curve = section.region.outer.curves.front();
      const auto uv = curve.point(curve.domain_start());
      const auto p = section.plane.origin + section.plane.x_axis * uv.x +
                     cross(section.plane.normal, section.plane.x_axis) * uv.y;
      const auto distance = kernel.face_distance(role.entity, p);
      REQUIRE(distance);
      CHECK_THAT(*distance, WithinAbs(0, 1e-6));
      ++caps;
    }
    const auto edges = kernel.edges(role.entity);
    REQUIRE(edges);
    REQUIRE_FALSE(edges->empty());
    for (const auto& edge : *edges) CHECK(edge->owner() == result.shape);
  }
  CHECK(caps == 2);
  CHECK(covered.Extent() == faces.Extent());
  for (std::size_t s = 0; s < input.sections.size(); ++s)
    for (std::size_t c = 0; c < input.sections[s].region.outer.curves.size(); ++c) {
      REQUIRE(sources.contains({s, c}));
      const auto& section = input.sections[s];
      const auto& curve = section.region.outer.curves[c];
      const auto y = cross(section.plane.normal, section.plane.x_axis);
      for (int i = 0; i <= 8; ++i) {
        const auto uv =
            curve.point(curve.domain_start() + (curve.domain_end() - curve.domain_start()) * i / 8);
        const auto p = section.plane.origin + section.plane.x_axis * uv.x + y * uv.y;
        double distance = std::numeric_limits<double>::infinity();
        for (const auto& face : sources.at({s, c})) {
          const auto d = kernel.face_distance(face, p);
          REQUIRE(d);
          distance = std::min(distance, *d);
        }
        CHECK_THAT(distance, WithinAbs(0, 1e-6));
      }
    }
  CHECK(result.history.empty());
  CHECK(result.estimated_bytes > result.shape->estimated_bytes());
}
void check_frustum(const Measures& m) {
  CHECK_THAT(m.volume, WithinAbs(5600, 1e-5));
  CHECK_THAT(m.area, WithinAbs(400 + 30 * std::sqrt(916) + 24 * std::sqrt(925), 1e-5));
  CHECK_THAT(m.centroid.x, WithinAbs(0, 1e-7));
  CHECK_THAT(m.centroid.y, WithinAbs(0, 1e-7));
  CHECK_THAT(m.centroid.z, WithinAbs(30.0 * 17 / 28, 1e-7));
  CHECK(m.solids == 1);
  CHECK(m.faces == 6);
  CHECK(m.edges == 12);
}
std::string serialized(const ShapePtr& shape) {
  std::ostringstream stream;
  BRepTools::Write(std::dynamic_pointer_cast<const detail::Snapshot>(shape)->geometry, stream);
  return stream.str();
}
std::string serialized(const Loft& input) {
  std::ostringstream stream;
  stream.precision(17);
  for (const auto& section : input.sections) {
    const auto& p = section.plane;
    for (const auto v : {p.origin, p.x_axis, p.normal}) stream << v.x << ',' << v.y << ',' << v.z;
    for (const auto& curve : section.region.outer.curves) {
      for (const auto point : curve.points()) stream << point.x << ',' << point.y;
      for (const auto w : curve.weights()) stream << w << ',';
      for (const auto k : curve.knots().knots()) stream << k << ',';
    }
  }
  return stream.str();
}
}  // namespace

TEST_CASE("solid loft has independent frustum mass and provenance from both sections",
          "[loft-kernel]") {
  auto kernel = make_occt_kernel().value();
  CHECK(kernel->capabilities().loft);
  CHECK(kernel->identity().find("loft=") != std::string_view::npos);
  auto input = frustum();
  const auto before = serialized(input);
  auto result = build(*kernel, input);
  check_frustum(result.shape->measures());
  check_provenance(*kernel, result, input);
  CHECK(serialized(input) == before);
}

TEST_CASE("smooth loft passes through three sections without silently becoming ruled",
          "[loft-kernel]") {
  auto kernel = make_occt_kernel().value();
  auto input = frustum();
  input.sections.back().region = rectangle();
  input.sections.insert(input.sections.begin() + 1, {{{0, 0, 15}}, rectangle(1.5)});
  const auto result = build(*kernel, input);
  // Uniformly spaced sections give s(t) = 1 + 2t - 2t²; integral(s², 0..1) = 9/5.
  CHECK_THAT(result.shape->measures().volume, WithinAbs(4320, 1e-5));
  CHECK_THAT(result.shape->measures().centroid.z, WithinAbs(15, 1e-7));
  CHECK(result.shape->measures().faces == 6);
  check_provenance(*kernel, result, input);
  std::reverse(input.sections.begin(), input.sections.end());
  const auto reverse = build(*kernel, input);
  CHECK_THAT(reverse.shape->measures().volume, WithinAbs(4320, 1e-5));
  CHECK_THAT(reverse.shape->measures().area, WithinAbs(result.shape->measures().area, 1e-5));
  check_provenance(*kernel, reverse, input);
}

TEST_CASE("loft supports rational circular sections and placed parallel planes", "[loft-kernel]") {
  auto kernel = make_occt_kernel().value();
  Loft input{{{{{7, -5, 9}, {0, 1, 0}, {1, 0, 0}}, {{{make_circle({}, 3)}}, {}}},
              {{{17, -5, 9}, {0, 1, 0}, {1, 0, 0}}, {{{make_circle({}, 6)}}, {}}}}};
  SECTION("same plane normals") {}
  SECTION("opposite plane normals") { input.sections.back().plane.normal = {-1, 0, 0}; }
  const auto result = build(*kernel, input);
  const auto& m = result.shape->measures();
  CHECK_THAT(m.volume, WithinAbs(210 * kPi, 1e-5));
  CHECK_THAT(m.area, WithinAbs(45 * kPi + 9 * kPi * std::sqrt(109), 1e-5));
  CHECK_THAT(m.centroid.x, WithinAbs(7 + 10.0 * 17 / 28, 1e-7));
  CHECK_THAT(m.centroid.y, WithinAbs(-5, 1e-7));
  CHECK_THAT(m.centroid.z, WithinAbs(9, 1e-7));
  check_provenance(*kernel, result, input);
}

TEST_CASE("loft preserves nonrational spline profiles with independent area and perimeter",
          "[loft-kernel]") {
  auto kernel = make_occt_kernel().value();
  const std::array<Vec2, 3> poles{{{0, 0}, {5, -6}, {10, 0}}};
  Region region{{{make_bezier(poles), make_line(Vec2{10, 0}, {})}}, {}};
  Loft input{{{{}, region}, {{{0, 0, 12}}, region}}};
  const auto before = serialized(input);
  const auto result = build(*kernel, input);
  double integral = 0;
  constexpr int n = 10'000;
  for (int i = 0; i <= n; ++i) {
    const double t = static_cast<double>(i) / n, dy = 24 * t - 12;
    integral += (i == 0 || i == n ? 1 : (i % 2 ? 4 : 2)) * std::hypot(10, dy);
  }
  const double perimeter = 10 + integral / (3 * n);
  const auto& m = result.shape->measures();
  CHECK_THAT(m.volume, WithinAbs(240, 1e-5));
  CHECK_THAT(m.area, WithinAbs(40 + 12 * perimeter, 1e-5));
  CHECK_THAT(m.centroid.x, WithinAbs(5, 1e-7));
  CHECK_THAT(m.centroid.y, WithinAbs(-1.2, 1e-7));
  CHECK_THAT(m.centroid.z, WithinAbs(6, 1e-7));
  check_provenance(*kernel, result, input);
  CHECK(serialized(input) == before);
}

TEST_CASE("loft integrates tapered elliptic and spline sides against independent quadrature",
          "[loft-kernel]") {
  auto kernel = make_occt_kernel().value();
  auto input = frustum();
  double expected_volume = 0, expected_area = 0;
  constexpr int n = 20'000;
  double integral = 0;
  SECTION("rational elliptic sections") {
    for (std::size_t s = 0; s < 2; ++s) {
      const double scale = s == 0 ? 1 : 2;
      input.sections[s].plane.origin.z = s == 0 ? 0 : 12;
      input.sections[s].region = {{{make_arc({}, 5 * scale, 3 * scale, 0, 0, 2 * kPi)}}, {}};
    }
    for (int i = 0; i <= n; ++i) {
      const double t = 2 * kPi * i / n;
      const double jacobian =
          std::sqrt(144 * (25 * std::pow(std::sin(t), 2) + 9 * std::pow(std::cos(t), 2)) + 225);
      integral += (i == 0 || i == n ? 1 : (i % 2 ? 4 : 2)) * jacobian;
    }
    expected_volume = 420 * kPi;
    expected_area = 75 * kPi + 1.5 * integral * (2 * kPi) / (3 * n);
  }
  SECTION("nonrational quadratic sections") {
    for (std::size_t s = 0; s < 2; ++s) {
      const double scale = s == 0 ? 1 : 2;
      const std::array<Vec2, 3> poles{{{0, 0}, {5 * scale, -6 * scale}, {10 * scale, 0}}};
      input.sections[s].plane.origin.z = s == 0 ? 0 : 12;
      input.sections[s].region = {{{make_bezier(poles), make_line(Vec2{10 * scale, 0}, {})}}, {}};
    }
    for (int i = 0; i <= n; ++i) {
      const double t = static_cast<double>(i) / n;
      const double x = 10 * t, y = -12 * t * (1 - t), dy = 24 * t - 12;
      const double jacobian = std::sqrt(144 * (100 + dy * dy) + std::pow(10 * y - dy * x, 2));
      integral += (i == 0 || i == n ? 1 : (i % 2 ? 4 : 2)) * jacobian;
    }
    expected_volume = 560;
    expected_area = 100 + 180 + 1.5 * integral / (3 * n);
  }
  const auto result = build(*kernel, input);
  CHECK_THAT(result.shape->measures().volume, WithinAbs(expected_volume, 1e-5));
  CHECK_THAT(result.shape->measures().area, WithinAbs(expected_area, 1e-5));
  check_provenance(*kernel, result, input);
}

TEST_CASE("loft preserves exact rational sections through a smooth bulge", "[loft-kernel]") {
  auto kernel = make_occt_kernel().value();
  Loft input{{{{}, {{{make_circle({}, 3)}}, {}}},
              {{{0, 0, 15}}, {{{make_circle({}, 4.5)}}, {}}},
              {{{0, 0, 30}}, {{{make_circle({}, 3)}}, {}}}}};
  const auto result = build(*kernel, input);
  constexpr int n = 20'000;
  double integral = 0;
  for (int i = 0; i <= n; ++i) {
    const double t = static_cast<double>(i) / n;
    const double r = 3 * (1 + 2 * t - 2 * t * t), dr = 6 - 12 * t;
    integral += (i == 0 || i == n ? 1 : (i % 2 ? 4 : 2)) * r * std::hypot(30, dr);
  }
  CHECK_THAT(result.shape->measures().volume, WithinAbs(486 * kPi, 1e-5));
  CHECK_THAT(result.shape->measures().area,
             WithinAbs(18 * kPi + 2 * kPi * integral / (3 * n), 1e-5));
  check_provenance(*kernel, result, input);
}

TEST_CASE("loft tracks authored multi-span curves without requiring equal curve counts",
          "[loft-kernel]") {
  auto kernel = make_occt_kernel().value();
  auto input = frustum();
  const std::array<Vec2, 5> points{{{-10, -8}, {10, -8}, {10, 8}, {-10, 8}, {-10, -8}}};
  input.sections.back().region = {{{make_polyline(points)}}, {}};
  const auto result = build(*kernel, input);
  check_frustum(result.shape->measures());
  check_provenance(*kernel, result, input);
}

TEST_CASE("loft resolves cyclic starts and reversed winding on nonconvex compatible sections",
          "[loft-kernel]") {
  auto kernel = make_occt_kernel().value();
  const std::vector<Vec2> l{{0, 0}, {2, 0}, {2, 1}, {1, 1}, {1, 2}, {0, 2}};
  auto scaled = l;
  for (auto& p : scaled) p = p * 2;
  std::rotate(scaled.begin(), scaled.begin() + 3, scaled.end());
  SECTION("different cyclic start") {}
  SECTION("opposite winding") { std::reverse(scaled.begin(), scaled.end()); }
  Loft input{{{{}, polygon(l)}, {{{0, 0, 10}}, polygon(scaled)}}};
  const auto result = build(*kernel, input);
  CHECK_THAT(result.shape->measures().volume, WithinAbs(70, 1e-5));
  check_provenance(*kernel, result, input);
}

TEST_CASE("loft mesh STEP and retained entities preserve the immutable exact snapshot",
          "[loft-kernel]") {
  auto kernel = make_occt_kernel().value();
  const auto input = frustum();
  auto result = build(*kernel, input);
  const auto before = serialized(result.shape);
  auto mesh = kernel->tessellate(result.shape, {});
  REQUIRE(mesh);
  CHECK(mesh->faces.size() == 6);
  CHECK_FALSE(mesh->indices.empty());
  double mesh_volume = 0, mesh_area = 0;
  for (std::size_t i = 0; i < mesh->indices.size(); i += 3) {
    const auto a = mesh->vertices[mesh->indices[i]].position;
    const auto b = mesh->vertices[mesh->indices[i + 1]].position;
    const auto c = mesh->vertices[mesh->indices[i + 2]].position;
    mesh_volume += dot(a, cross(b, c)) / 6;
    mesh_area += length(cross(b - a, c - a)) / 2;
  }
  CHECK_THAT(mesh_volume, WithinAbs(5600, 1e-5));
  CHECK_THAT(mesh_area, WithinAbs(result.shape->measures().area, 1e-5));
  const auto text = kernel->export_step({result.shape});
  REQUIRE(text);
  std::istringstream stream(*text);
  STEPControl_Reader reader;
  REQUIRE(reader.ReadStream("loft.step", stream) == IFSelect_RetDone);
  reader.SetSystemLengthUnit(1);
  REQUIRE(reader.TransferRoots() > 0);
  auto reloaded = detail::snapshot(reader.OneShape());
  REQUIRE(reloaded);
  check_frustum((*reloaded)->measures());
  CHECK(serialized(result.shape) == before);
  const auto retained = result.generated.front().entity;
  result.shape.reset();
  result.generated.clear();
  kernel.reset();
  REQUIRE(retained->owner());
  check_frustum(retained->owner()->measures());
  kernel = make_occt_kernel().value();
  REQUIRE(kernel->edges(retained));
  CHECK(serialized(retained->owner()) == before);
}

TEST_CASE("loft rejects unsupported or invalid sections atomically and preserves earlier results",
          "[loft-kernel]") {
  auto kernel = make_occt_kernel().value();
  const auto result = build(*kernel, frustum());
  const auto before = serialized(result.shape);
  auto input = frustum();
  SECTION("empty") { input.sections.clear(); }
  SECTION("single section") { input.sections.pop_back(); }
  SECTION("section bound") { input.sections.resize(33, input.sections.front()); }
  SECTION("coincident planes") { input.sections.back().plane = input.sections.front().plane; }
  SECTION("planes inside linear tolerance") { input.sections.back().plane.origin.z = 1e-8; }
  SECTION("nonparallel planes") { input.sections.back().plane.normal = {0, 1, 0}; }
  SECTION("reversed section order in the middle") {
    input.sections.insert(input.sections.begin() + 1, {{{0, 0, 40}}, rectangle()});
  }
  SECTION("invalid axes") { input.sections.front().plane.x_axis = {2, 0, 0}; }
  SECTION("nonfinite plane") {
    input.sections.back().plane.origin.x = std::numeric_limits<double>::infinity();
  }
  SECTION("holes") { input.sections.front().region.holes.push_back(rectangle(0.1).outer); }
  SECTION("empty boundary") { input.sections.front().region.outer.curves.clear(); }
  SECTION("open boundary") { input.sections.front().region.outer.curves.pop_back(); }
  SECTION("authored curve collapses at kernel precision") {
    auto& curves = input.sections.front().region.outer.curves;
    curves.insert(curves.begin() + 1, make_line(Vec2{5, -4}, {5, -4}));
  }
  SECTION("self intersecting boundary") {
    input.sections.front().region = polygon({{-2, -2}, {2, 2}, {-2, 2}, {2, -2}});
  }
  SECTION("degenerate collinear boundary") {
    input.sections.front().region = polygon({{0, 0}, {1, 0}, {2, 0}});
  }
  SECTION("smooth interpolation collapses between positive sections") {
    input.sections.insert(input.sections.begin() + 1, {{{0, 0, 10}}, rectangle(0.1)});
    input.sections.insert(input.sections.begin() + 2, {{{0, 0, 20}}, rectangle(0.1)});
    input.sections.back().region = rectangle();
  }
  SECTION("curve bound") {
    auto& curves = input.sections.front().region.outer.curves;
    curves.resize(129, curves.front());
  }
  SECTION("decomposed edge bound") {
    std::vector<Vec2> points;
    for (int i = 0; i <= 1025; ++i)
      points.push_back({10 * std::cos(2 * kPi * i / 1025), 10 * std::sin(2 * kPi * i / 1025)});
    input.sections.front().region = {{{make_polyline(points)}}, {}};
  }
  const auto failed = kernel->loft(input);
  REQUIRE_FALSE(failed);
  CHECK(failed.error().code == "kernel");
  CHECK_FALSE(failed.error().message.empty());
  CHECK(serialized(result.shape) == before);
  REQUIRE(kernel->tessellate(result.shape, {}));
}
