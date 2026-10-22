// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// RFC 0014 meshing: a kernel solid meshed by Netgen, with every boundary triangle tagged by the
// number face_number() gives the kernel face it lies on.
#include "geometry.hpp"
#include "nf/math/constructors.hpp"
#include "plain_mesh.hpp"

#include <unistd.h>
#include <cmath>
#include <filesystem>
#include <map>
#include <numbers>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf;
using namespace nf::study;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {
std::filesystem::path temporary(const char* name) {
  return std::filesystem::temp_directory_path() /
         ("nf-meshing-" + std::to_string(getpid()) + "-" + name + ".brep");
}
double tet_volume(const PlainMesh& m, std::size_t t) {
  const auto p = [&](std::size_t k, std::size_t c) {
    return m.points[3 * static_cast<std::size_t>(m.tets[4 * t + k]) + c];
  };
  const auto d = [&](std::size_t k, std::size_t c) { return p(k, c) - p(0, c); };
  return std::abs(d(1, 0) * (d(2, 1) * d(3, 2) - d(2, 2) * d(3, 1)) -
                  d(1, 1) * (d(2, 0) * d(3, 2) - d(2, 2) * d(3, 0)) +
                  d(1, 2) * (d(2, 0) * d(3, 1) - d(2, 1) * d(3, 0))) /
         6;
}
}  // namespace

TEST_CASE("boundary triangles carry the number of the kernel face they lie on", "[study][mesh]") {
  auto kernel = kernel::make_occt_kernel().value();
  // A 60 x 40 mm plate, 10 mm thick, with a 12 mm hole: planes and a cylinder.
  using math::make_circle;
  using math::make_line;
  using math::Vec2;
  std::vector<std::vector<math::NurbsCurve2>> paths{
      {make_line(Vec2{}, {60, 0}), make_line(Vec2{60, 0}, {60, 40}),
       make_line(Vec2{60, 40}, {0, 40}), make_line(Vec2{0, 40}, {})},
      {make_circle(Vec2{30, 20}, 6)}};
  const auto plate =
      kernel->extrude({{{}}, math::regions(paths, math::FillRule::kEvenOdd).value(), {0, 0, 10}})
          .value()
          .shape;
  const auto path = temporary("plate");
  REQUIRE(write_brep(plate, path.string()));
  const auto mesh = mesh_brep(path.string(), 4);
  std::filesystem::remove(path);
  REQUIRE(mesh.tet_count() > 100);
  double volume = 0;
  for (std::size_t t = 0; t < mesh.tet_count(); ++t) volume += tet_volume(mesh, t);
  // Straight-edged tetrahedra facet the 6 mm bore into a polygon with less area than the
  // circle, so the mesh holds slightly more material than the exact solid (0.23% at 4 mm).
  CHECK(volume > plate->measures().volume);
  CHECK_THAT(volume, WithinRel(plate->measures().volume, 5e-3));
  // Every node of every boundary triangle lies on its tagged kernel face: on the plane of a
  // planar face, or 6 mm from the bore's axis.
  std::map<int, kernel::FaceInfo> faces;
  for (const auto& f : kernel->faces(plate).value())
    faces.emplace(face_number(f.entity).value(), f);
  REQUIRE(static_cast<int>(faces.size()) == face_count(plate).value());
  std::map<int, std::size_t> counts;
  for (std::size_t s = 0; s < mesh.triangle_count(); ++s) {
    const auto& f = faces.at(mesh.faces[s]);
    ++counts[mesh.faces[s]];
    for (std::size_t k = 0; k < 3; ++k) {
      const auto* p = &mesh.points[3 * static_cast<std::size_t>(mesh.triangles[3 * s + k])];
      const math::Vec3 q{p[0], p[1], p[2]};
      if (f.plane) {
        CHECK_THAT(math::dot(f.plane->normal, q - f.plane->origin), WithinAbs(0, 1e-9));
      } else {
        REQUIRE(f.cylinder);
        CHECK_THAT(std::hypot(q.x - 30, q.y - 20), WithinAbs(6, 1e-9));
      }
    }
  }
  CHECK(counts.size() == faces.size());  // every face is meshed
}

TEST_CASE("meshing refuses what it cannot mesh", "[study][mesh]") {
  CHECK_THROWS(mesh_brep("/nonexistent/solid.brep", 4));
  auto kernel = kernel::make_occt_kernel().value();
  using math::make_line;
  using math::Vec2;
  std::vector<std::vector<math::NurbsCurve2>> square{
      {make_line(Vec2{}, {1, 0}), make_line(Vec2{1, 0}, {1, 1}), make_line(Vec2{1, 1}, {0, 1}),
       make_line(Vec2{0, 1}, {})}};
  const auto box =
      kernel->extrude({{{}}, math::regions(square, math::FillRule::kEvenOdd).value(), {0, 0, 1}})
          .value()
          .shape;
  const auto path = temporary("unit");
  REQUIRE(write_brep(box, path.string()));
  CHECK_THROWS(mesh_brep(path.string(), 0));
  std::filesystem::remove(path);
}
