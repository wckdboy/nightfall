// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "nf/kernel/kernel.hpp"
#include "nf/math/constructors.hpp"
#include "nf/math/refine.hpp"
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
Region rectangle() {
  return {{{make_line(Vec2{-2, -1}, Vec2{2, -1}), make_line(Vec2{2, -1}, Vec2{2, 1}),
            make_line(Vec2{2, 1}, Vec2{-2, 1}), make_line(Vec2{-2, 1}, Vec2{-2, -1})}},
          {}};
}
Sweep straight() {
  return {{{}, {0, 1, 0}, {1, 0, 0}}, rectangle(), {}, {make_line(Vec2{}, Vec2{30, 0})}};
}
Sweep bend() {
  return {{{20, 0, 0}, {1, 0, 0}, {0, 1, 0}},
          rectangle(),
          {},
          {make_arc(Vec2{}, 20, 20, 0, 0, kPi / 2)}};
}
OperationResult build(IKernel& kernel, const Sweep& input) {
  const auto result = kernel.sweep(input);
  INFO((result ? "success" : result.error().message));
  REQUIRE(result);
  return *result;
}
void provenance(IKernel& kernel, const OperationResult& result, const Sweep& input) {
  const auto snapshot = std::dynamic_pointer_cast<const detail::Snapshot>(result.shape);
  REQUIRE(snapshot);
  TopTools_IndexedMapOfShape faces, covered;
  TopExp::MapShapes(snapshot->geometry, TopAbs_FACE, faces);
  std::map<std::size_t, std::vector<EntityPtr>> sources;
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
      CHECK(role.from->section == 0);
      REQUIRE(role.from->curve < input.region.outer.curves.size());
      sources[role.from->curve].push_back(role.entity);
    } else {
      CHECK((role.role == "cap_start" || role.role == "cap_end"));
      CHECK_FALSE(role.from);
      const auto plane = kernel.face_plane(role.entity);
      REQUIRE(plane);
      const auto& curve = role.role == "cap_start" ? input.path.front() : input.path.back();
      const double u = role.role == "cap_start" ? curve.domain_start() : curve.domain_end();
      std::array<Vec2, 2> derivatives;
      curve.derivatives(u, 1, derivatives);
      const auto tangent = *normalized(derivatives[1]);
      const auto y = cross(input.path_plane.normal, input.path_plane.x_axis);
      const auto p = input.path_plane.origin + input.path_plane.x_axis * derivatives[0].x +
                     y * derivatives[0].y;
      const auto n = (input.path_plane.x_axis * tangent.x + y * tangent.y) *
                     (role.role == "cap_start" ? -1 : 1);
      CHECK_THAT(dot(plane->origin - p, n), WithinAbs(0, 1e-6));
      CHECK_THAT(nf::math::length(plane->normal - n), WithinAbs(0, 1e-6));
      ++caps;
    }
    const auto edges = kernel.edges(role.entity);
    REQUIRE(edges);
    REQUIRE_FALSE(edges->empty());
    for (const auto& edge : *edges) CHECK(edge->owner() == result.shape);
  }
  CHECK(caps == 2);
  CHECK(covered.Extent() == faces.Extent());
  const auto y = cross(input.plane.normal, input.plane.x_axis);
  for (std::size_t c = 0; c < input.region.outer.curves.size(); ++c) {
    REQUIRE(sources.contains(c));
    const auto& curve = input.region.outer.curves[c];
    for (int i = 0; i <= 8; ++i) {
      const auto uv =
          curve.point(curve.domain_start() + (curve.domain_end() - curve.domain_start()) * i / 8);
      const auto p = input.plane.origin + input.plane.x_axis * uv.x + y * uv.y;
      double distance = std::numeric_limits<double>::infinity();
      for (const auto& face : sources.at(c)) {
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
std::string serialized(const ShapePtr& shape) {
  std::ostringstream stream;
  BRepTools::Write(std::dynamic_pointer_cast<const detail::Snapshot>(shape)->geometry, stream);
  return stream.str();
}
std::string serialized(const Sweep& input) {
  std::ostringstream stream;
  stream.precision(17);
  for (const auto& p : {input.plane, input.path_plane})
    for (const auto v : {p.origin, p.x_axis, p.normal}) stream << v.x << ',' << v.y << ',' << v.z;
  for (const auto* curves : {&input.path, &input.region.outer.curves})
    for (const auto& curve : *curves) {
      for (const auto p : curve.points()) stream << p.x << ',' << p.y;
      for (const auto w : curve.weights()) stream << w << ',';
      for (const auto k : curve.knots().knots()) stream << k << ',';
    }
  return stream.str();
}
double length(const NurbsCurve2& curve) {
  // Independent positive composite Simpson integral, not OCCT or the production integrator.
  constexpr int n = 16384;
  const double a = curve.domain_start(), h = (curve.domain_end() - a) / n;
  double sum = 0;
  for (int i = 0; i <= n; ++i) {
    std::array<Vec2, 2> derivatives;
    curve.derivatives(a + h * i, 1, derivatives);
    sum += nf::math::length(derivatives[1]) * (i == 0 || i == n ? 1 : i % 2 ? 4 : 2);
  }
  return sum * h / 3;
}
}  // namespace

TEST_CASE("solid sweep has independent straight mass and complete profile provenance",
          "[sweep-kernel]") {
  auto kernel = make_occt_kernel().value();
  CHECK(kernel->capabilities().sweep);
  CHECK(kernel->identity().find("sweep=") != std::string_view::npos);
  const auto input = straight();
  const auto before = serialized(input);
  const auto result = build(*kernel, input);
  const auto& m = result.shape->measures();
  CHECK_THAT(m.volume, WithinAbs(240, 1e-6));
  CHECK_THAT(m.area, WithinAbs(376, 1e-6));
  CHECK_THAT(m.centroid.x, WithinAbs(15, 1e-7));
  CHECK_THAT(m.centroid.y, WithinAbs(0, 1e-7));
  CHECK_THAT(m.centroid.z, WithinAbs(0, 1e-7));
  CHECK(m.solids == 1);
  CHECK(m.faces == 6);
  provenance(*kernel, result, input);
  CHECK(serialized(input) == before);
}

TEST_CASE("planar bend sweep preserves radial orientation without silent frame rotation",
          "[sweep-kernel]") {
  auto kernel = make_occt_kernel().value();
  auto input = bend();
  SECTION("rectangle") {
    const auto result = build(*kernel, input);
    const auto& m = result.shape->measures();
    CHECK_THAT(m.volume, WithinAbs(80 * kPi, 1e-5));
    CHECK_THAT(m.area, WithinAbs(16 + 120 * kPi, 1e-5));
    const double center = (20 + 4.0 / 60) * 2 / kPi;
    CHECK_THAT(m.centroid.x, WithinAbs(center, 1e-6));
    CHECK_THAT(m.centroid.y, WithinAbs(center, 1e-6));
    CHECK_THAT(m.centroid.z, WithinAbs(0, 1e-7));
    provenance(*kernel, result, input);
  }
  SECTION("rational circle") {
    input.region = {{{make_circle(Vec2{}, 2)}}, {}};
    const auto result = build(*kernel, input);
    const auto& m = result.shape->measures();
    CHECK_THAT(m.volume, WithinAbs(40 * kPi * kPi, 1e-5));
    CHECK_THAT(m.area, WithinAbs(40 * kPi * kPi + 8 * kPi, 1e-5));
    CHECK_THAT(m.centroid.x, WithinAbs((20 + 1.0 / 20) * 2 / kPi, 1e-6));
    CHECK_THAT(m.centroid.y, WithinAbs((20 + 1.0 / 20) * 2 / kPi, 1e-6));
    provenance(*kernel, result, input);
  }
}

TEST_CASE("sweep preserves exact nonrational spline profile geometry", "[sweep-kernel]") {
  auto kernel = make_occt_kernel().value();
  auto input = straight();
  input.region = {{{make_bezier(std::array<Vec2, 3>{{{0, 0}, {8, 8}, {16, 0}}}),
                    make_line(Vec2{16, 0}, Vec2{})}},
                  {}};
  const auto result = build(*kernel, input);
  CHECK_THAT(result.shape->measures().volume, WithinAbs(1280, 1e-5));
  const double perimeter = 16 + 8 * (std::sqrt(2.0) + std::asinh(1.0));
  CHECK_THAT(result.shape->measures().area, WithinAbs(256.0 / 3 + 30 * perimeter, 1e-5));
  provenance(*kernel, result, input);
}

TEST_CASE("sweep preserves authored traversal and profile roll on bends", "[sweep-kernel]") {
  auto kernel = make_occt_kernel().value();
  auto input = bend();
  double radial_variance = 4.0 / 3;
  SECTION("reverse traversal") {
    input.path.front() = reverse(input.path.front());
    input.plane = {{0, 20, 0}, {0, 1, 0}, {1, 0, 0}};
  }
  SECTION("quarter-turn profile roll") {
    input.plane.x_axis = {0, 0, 1};
    radial_variance = 1.0 / 3;
  }
  const auto before = serialized(input);
  const auto result = build(*kernel, input);
  const auto& m = result.shape->measures();
  CHECK_THAT(m.volume, WithinAbs(80 * kPi, 1e-5));
  CHECK_THAT(m.area, WithinAbs(16 + 120 * kPi, 1e-5));
  CHECK_THAT(m.centroid.x, WithinAbs((20 + radial_variance / 20) * 2 / kPi, 1e-6));
  CHECK_THAT(m.centroid.y, WithinAbs((20 + radial_variance / 20) * 2 / kPi, 1e-6));
  provenance(*kernel, result, input);
  CHECK(serialized(input) == before);
}

TEST_CASE("spline sweep uses constant planar binormal through curvature changes and inflections",
          "[sweep-kernel]") {
  auto kernel = make_occt_kernel().value();
  auto input = straight();
  input.region = {{{make_circle(Vec2{}, 0.5)}}, {}};
  SECTION("quadratic parabola") {
    input.path = {make_bezier(std::array<Vec2, 3>{{{0, 0}, {10, 0}, {20, 10}}})};
  }
  SECTION("cubic inflection") {
    input.path = {make_bezier(std::array<Vec2, 4>{{{0, 0}, {10, 10}, {20, -10}, {30, 0}}})};
    const auto n = std::sqrt(0.5);
    input.plane.normal = {n, n, 0};
    input.plane.x_axis = {-n, n, 0};
  }
  const double arc_length = length(input.path.front());
  const auto result = build(*kernel, input);
  CHECK_THAT(result.shape->measures().volume, WithinAbs(kPi / 4 * arc_length, 1e-4));
  CHECK_THAT(result.shape->measures().area, WithinAbs(kPi * arc_length + kPi / 2, 1e-4));
  provenance(*kernel, result, input);
}

TEST_CASE("sweep joins tangent line arc and line paths with complete side coverage",
          "[sweep-kernel]") {
  auto kernel = make_occt_kernel().value();
  auto input = straight();
  input.region = {{{make_circle(Vec2{}, 1)}}, {}};
  input.path = {make_line(Vec2{}, Vec2{10, 0}), make_arc(Vec2{10, 10}, 10, 10, 0, -kPi / 2, 0),
                make_line(Vec2{20, 10}, Vec2{20, 20})};
  const auto result = build(*kernel, input);
  const double arc_length = 20 + 5 * kPi;
  CHECK_THAT(result.shape->measures().volume, WithinAbs(kPi * arc_length, 1e-5));
  CHECK_THAT(result.shape->measures().area, WithinAbs(2 * kPi * (arc_length + 1), 1e-5));
  provenance(*kernel, result, input);
}

TEST_CASE("sweep respects placed path planes and authored profile roll", "[sweep-kernel]") {
  auto kernel = make_occt_kernel().value();
  auto input = straight();
  input.path_plane = {{1000000, -2000000, 3000000}, {0, 0, 1}, {0, 1, 0}};
  input.plane = {input.path_plane.origin, {1, 0, 0}, {0, 0, 1}};
  const auto result = build(*kernel, input);
  const auto& m = result.shape->measures();
  CHECK_THAT(m.volume, WithinAbs(240, 1e-5));
  CHECK_THAT(m.area, WithinAbs(376, 1e-5));
  CHECK_THAT(m.centroid.x, WithinAbs(1000000, 1e-6));
  CHECK_THAT(m.centroid.y, WithinAbs(-2000000, 1e-6));
  CHECK_THAT(m.centroid.z, WithinAbs(3000015, 1e-6));
  provenance(*kernel, result, input);
}

TEST_CASE("sweep mesh STEP and retained readers preserve exact immutable snapshots",
          "[sweep-kernel]") {
  auto kernel = make_occt_kernel().value();
  auto input = bend();
  const auto original = build(*kernel, input);
  const auto before = serialized(original.shape);
  const auto mesh = kernel->tessellate(original.shape, {});
  REQUIRE(mesh);
  CHECK_FALSE(mesh->indices.empty());
  CHECK(mesh->faces.size() == original.shape->measures().faces);
  for (const auto& face : mesh->faces) CHECK(face.face->owner() == original.shape);
  const auto step = kernel->export_step({original.shape});
  REQUIRE(step);
  std::istringstream stream(*step);
  STEPControl_Reader reader;
  REQUIRE(reader.ReadStream("sweep.step", stream) == IFSelect_RetDone);
  REQUIRE(reader.TransferRoots() > 0);
  const auto reloaded = detail::snapshot(reader.OneShape());
  REQUIRE(reloaded);
  CHECK_THAT((*reloaded)->measures().volume, WithinAbs(80 * kPi, 1e-5));
  CHECK_THAT((*reloaded)->measures().area, WithinAbs(16 + 120 * kPi, 1e-5));
  input.region = rectangle();
  input.path = {make_line(Vec2{20, 0}, Vec2{20, 10})};
  const auto second = build(*kernel, input);
  CHECK_THAT(second.shape->measures().volume, WithinAbs(80, 1e-6));
  CHECK(serialized(original.shape) == before);
  const auto retained = original.generated.front().entity;
  kernel.reset();
  CHECK(retained->owner() == original.shape);
  CHECK(serialized(retained->owner()) == before);
}

TEST_CASE("sweep rejects unsupported or unsafe paths atomically", "[sweep-kernel]") {
  auto kernel = make_occt_kernel().value();
  const auto prior = build(*kernel, straight());
  const auto before = serialized(prior.shape);
  auto input = straight();
  SECTION("empty path") { input.path.clear(); }
  SECTION("closed path") { input.path = {make_circle(Vec2{}, 10)}; }
  SECTION("self-intersecting spine") {
    input = bend();
    input.path = {make_circle(Vec2{}, 20), make_line(Vec2{20, 0}, Vec2{20, 30})};
  }
  SECTION("too many path curves") {
    const auto curve = input.path.front();
    input.path.assign(33, curve);
  }
  SECTION("too many spans") {
    std::vector<Vec2> points;
    for (int i = 0; i <= 65; ++i) points.push_back({static_cast<double>(i), 0});
    input.path = {make_polyline(std::span<const Vec2>(points))};
  }
  SECTION("corner") {
    input.path = {make_polyline(std::array<Vec2, 3>{{{0, 0}, {10, 0}, {10, 10}}})};
  }
  SECTION("disconnected") {
    input.path = {make_line(Vec2{}, Vec2{10, 0}), make_line(Vec2{11, 0}, Vec2{20, 0})};
  }
  SECTION("degenerate tangent") {
    input.path = {make_bezier(std::array<Vec2, 3>{{{0, 0}, {0, 0}, {10, 0}}})};
  }
  SECTION("interior cusp") {
    input.path = {make_bezier(std::array<Vec2, 4>{{{0, 0}, {10, 0}, {-10, 0}, {20, 0}}})};
  }
  SECTION("too tight bend") {
    input = bend();
    input.region = {{{make_circle(Vec2{}, 25)}}, {}};
  }
  SECTION("profile origin is not the path start") { input.plane.origin = {1, 0, 0}; }
  SECTION("wrong initial profile normal") { input.plane = {}; }
  SECTION("invalid path frame") { input.path_plane.x_axis = {2, 0, 0}; }
  SECTION("nonfinite path frame") {
    input.path_plane.origin.x = std::numeric_limits<double>::infinity();
  }
  SECTION("empty profile") { input.region.outer.curves.clear(); }
  SECTION("too many profile curves") {
    const auto curve = input.region.outer.curves.front();
    input.region.outer.curves.assign(129, curve);
  }
  SECTION("holes") { input.region.holes.push_back({{make_circle(Vec2{}, 0.25)}}); }
  SECTION("open profile") { input.region.outer.curves.pop_back(); }
  SECTION("collapsed authored profile curve") {
    input.region.outer.curves.insert(input.region.outer.curves.begin(),
                                     make_line(Vec2{-2, -1}, Vec2{-2, -1}));
  }
  const auto failed = kernel->sweep(input);
  INFO((failed ? "unexpected success" : failed.error().message));
  REQUIRE_FALSE(failed);
  CHECK(failed.error().code == "kernel");
  CHECK_FALSE(failed.error().message.empty());
  CHECK(serialized(prior.shape) == before);
  const auto restored = build(*kernel, straight());
  CHECK_THAT(restored.shape->measures().volume, WithinAbs(240, 1e-6));
}
