// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// RFC 0013 interference and clearance over placed "occurrence/body" solids: analytic boxes.
#include "assembly_queries.hpp"
#include "nf/math/constructors.hpp"

#include <array>
#include <cmath>
#include <tuple>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf;
using namespace nf::kernel_service;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {
kernel::ShapePtr box(kernel::IKernel& kernel, math::Vec3 origin, math::Vec3 size) {
  using math::make_line;
  using math::Vec2;
  std::vector<std::vector<math::NurbsCurve2>> paths{
      {make_line(Vec2{}, {size.x, 0}), make_line(Vec2{size.x, 0}, {size.x, size.y}),
       make_line(Vec2{size.x, size.y}, {0, size.y}), make_line(Vec2{0, size.y}, {})}};
  return kernel
      .extrude({{origin}, math::regions(paths, math::FillRule::kEvenOdd).value(), {0, 0, size.z}})
      .value()
      .shape;
}
}  // namespace

TEST_CASE("interference pairs bodies of different occurrences and measures overlap",
          "[assembly][interference]") {
  auto kernel = kernel::make_occt_kernel().value();
  const std::vector<PlacedBody> bodies{
      {"base/plate", box(*kernel, {0, 0, 0}, {20, 20, 5})},
      // The base's own boss overlaps its plate: one occurrence, never reported.
      {"base/boss", box(*kernel, {5, 5, 4}, {4, 4, 4})},
      // 2 mm into the plate over 10 x 10 mm: 200 mm^3.
      {"lid/plate", box(*kernel, {0, 0, 3}, {10, 10, 6})},
      // Resting on the plate's top face: contact, no volume.
      {"shim/plate", box(*kernel, {12, 12, 5}, {4, 4, 1})},
      // Far away: its bounding box meets nothing, so it is never checked exactly.
      {"spare/plate", box(*kernel, {100, 0, 0}, {5, 5, 5})},
  };
  std::size_t checked = 0;
  const auto found = check_interference(*kernel, bodies, {}, &checked).value();
  REQUIRE(found.size() == 3);
  // Sorted by body ids, a < b.
  CHECK(found[0].a == "base/boss");
  CHECK(found[0].b == "lid/plate");
  CHECK_THAT(found[0].volume, WithinRel(4.0 * 4 * 4, 1e-6));  // the boss sits inside the lid
  CHECK(found[1].a == "base/plate");
  CHECK(found[1].b == "lid/plate");
  CHECK_THAT(found[1].volume, WithinRel(200.0, 1e-6));
  CHECK_FALSE(found[1].touching);
  CHECK(found[2].a == "base/plate");
  CHECK(found[2].b == "shim/plate");
  CHECK(found[2].touching);
  CHECK(found[2].volume == 0);
  // Only three pairs' boxes meet (boss-lid, plate-lid, plate-shim); the rest are skipped.
  CHECK(checked == 3);
  // Restricted to the shim, only its contact remains.
  const auto shim = check_interference(*kernel, bodies, {"shim"}).value();
  REQUIRE(shim.size() == 1);
  CHECK(shim[0].b == "shim/plate");
}

TEST_CASE("clearance is the smallest gap between two sets of bodies", "[assembly][interference]") {
  auto kernel = kernel::make_occt_kernel().value();
  const std::vector<PlacedBody> a{{"arm/link", box(*kernel, {0, 0, 0}, {10, 2, 2})},
                                  {"arm/pin", box(*kernel, {9, 0, 2}, {1, 1, 6})}};
  const std::vector<PlacedBody> b{{"stop/block", box(*kernel, {14, -5, 0}, {4, 10, 10})}};
  const auto c = measure_clearance(*kernel, a, b).value();
  CHECK_THAT(c.distance, WithinAbs(4, 1e-9));
  CHECK(c.b == "stop/block");
  CHECK_THAT(c.on_a.x, WithinAbs(10, 1e-9));
  CHECK_THAT(c.on_b.x, WithinAbs(14, 1e-9));
  CHECK_FALSE(measure_clearance(*kernel, a, {}).has_value());
}

TEST_CASE("assembly mass properties combine bodies and occurrences by parallel axes",
          "[assembly][mass]") {
  auto kernel = kernel::make_occt_kernel().value();
  // "arm" is two stacked 10 x 20 x 30 mm bodies (one 10 x 20 x 60 box); "weight" is a 10 mm
  // cube 100 mm away; "label" has no density and stays out of the total.
  const std::vector<PlacedBody> bodies{{"arm/lower", box(*kernel, {0, 0, 0}, {10, 20, 30})},
                                       {"arm/upper", box(*kernel, {0, 0, 30}, {10, 20, 30})},
                                       {"weight/cube", box(*kernel, {100, 0, 0}, {10, 10, 10})},
                                       {"label/plate", box(*kernel, {0, -40, 0}, {5, 5, 1})}};
  const auto mass = mass_properties(*kernel, bodies, {{"arm", 7850.0}, {"weight", 2700.0}}).value();
  REQUIRE(mass.occurrences.size() == 3);
  CHECK(mass.without_density == std::vector<std::string>{"label"});
  // kg = kg/m^3 x mm^3 x 1e-9; kg m^2 = kg x mm^2 x 1e-6.
  const double m_arm = 7850 * 12000e-9, m_weight = 2700 * 1000e-9;
  const auto& arm = mass.occurrences[0];
  CHECK(arm.id == "arm");
  CHECK_THAT(arm.volume, WithinRel(12000.0, 1e-9));
  CHECK_THAT(arm.mass, WithinRel(m_arm, 1e-9));
  CHECK_THAT(arm.centroid.z, WithinAbs(30, 1e-9));
  const std::array<double, 3> arm_box{10, 20, 60}, cube{10, 10, 10};
  const auto box_inertia = [](double m, const std::array<double, 3>& s, int axis) {
    const double a = s[static_cast<std::size_t>((axis + 1) % 3)],
                 b = s[static_cast<std::size_t>((axis + 2) % 3)];
    return m * (a * a + b * b) / 12 * 1e-6;
  };
  for (int k = 0; k < 3; ++k)
    CHECK_THAT(arm.inertia[static_cast<std::size_t>(4 * k)],
               WithinRel(box_inertia(m_arm, arm_box, k), 1e-9));
  CHECK_FALSE(mass.occurrences[2].density.has_value());
  CHECK(mass.occurrences[2].mass == 0);
  // The total, by the parallel-axis theorem about the combined centre of mass.
  const std::array<double, 3> c_arm{5, 10, 30}, c_weight{105, 5, 5};
  const double m = m_arm + m_weight;
  std::array<double, 3> com{};
  for (std::size_t i = 0; i < 3; ++i) com[i] = (m_arm * c_arm[i] + m_weight * c_weight[i]) / m;
  CHECK_THAT(mass.total.mass, WithinRel(m, 1e-9));
  CHECK_THAT(mass.total.centroid.x, WithinAbs(com[0], 1e-9));
  CHECK_THAT(mass.total.centroid.y, WithinAbs(com[1], 1e-9));
  CHECK_THAT(mass.total.centroid.z, WithinAbs(com[2], 1e-9));
  for (std::size_t i = 0; i < 3; ++i)
    for (std::size_t j = 0; j < 3; ++j) {
      double expected = 0;
      for (const auto& [mi, ci, size] :
           {std::tuple{m_arm, c_arm, arm_box}, std::tuple{m_weight, c_weight, cube}}) {
        const std::array<double, 3> d{ci[0] - com[0], ci[1] - com[1], ci[2] - com[2]};
        const double d2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        expected += (i == j ? box_inertia(mi, size, static_cast<int>(i)) : 0.0) +
                    mi * ((i == j ? d2 : 0.0) - d[i] * d[j]) * 1e-6;
      }
      CAPTURE(i, j);
      CHECK_THAT(mass.total.inertia[3 * i + j],
                 WithinAbs(expected, 1e-9 * std::abs(expected) + 1e-15));
    }
  // Restricted to one occurrence.
  const auto weight = mass_properties(*kernel, bodies, {{"weight", 2700.0}}, {"weight"}).value();
  REQUIRE(weight.occurrences.size() == 1);
  CHECK_THAT(weight.total.mass, WithinRel(m_weight, 1e-9));
}
