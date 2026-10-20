// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>
#include "nf_native.h"
#if NF_EXPECT_OCCT
#include <numbers>
#include "nf/kernel/kernel.hpp"
#include "nf/math/constructors.hpp"
#endif
using Catch::Matchers::WithinAbs;
namespace {
using Context = std::unique_ptr<nf_native_context, decltype(&nf_native_context_release)>;
using Result = std::unique_ptr<nf_native_result, decltype(&nf_native_result_release)>;
Context context() {
  nf_native_context* raw = nullptr;
  const auto status = nf_native_context_create(1, 1e-6, 1e-9, &raw);
  REQUIRE(status == (NF_EXPECT_OCCT ? NF_NATIVE_OK : NF_NATIVE_UNAVAILABLE));
  return {raw, nf_native_context_release};
}
#if NF_EXPECT_OCCT
struct Input {
  std::vector<std::vector<nf_native_curve2>> curves;
  std::vector<std::vector<std::vector<nf_native_point2>>> points;
  std::vector<std::vector<nf_native_loop2>> holes;
  std::vector<nf_native_region2> regions;
  nf_native_extrusion value{};
  nf_native_loop2 loop(const nf::math::Loop& source) {
    curves.emplace_back();
    points.emplace_back();
    for (const auto& curve : source.curves) {
      auto& poles = points.back().emplace_back();
      for (auto p : curve.points()) poles.push_back({p.x, p.y});
      curves.back().push_back({static_cast<uint32_t>(curve.degree()), curve.knots().knots().data(),
                               curve.knots().size(), poles.data(), poles.size(),
                               curve.weights().data(), curve.weights().size()});
    }
    return {curves.back().data(), curves.back().size()};
  }
  explicit Input(const nf::kernel::Extrusion& source) {
    for (const auto& region : source.regions) {
      auto outer = loop(region.outer);
      auto& h = holes.emplace_back();
      for (const auto& hole : region.holes) h.push_back(loop(hole));
      regions.push_back({outer, h.data(), h.size()});
    }
    auto copy = [](auto v, double* out) {
      out[0] = v.x;
      out[1] = v.y;
      out[2] = v.z;
    };
    copy(source.plane.origin, value.origin);
    copy(source.plane.x_axis, value.x_axis);
    copy(source.plane.normal, value.normal);
    copy(source.displacement, value.displacement);
    value.regions = regions.data();
    value.region_count = regions.size();
  }
};
nf::kernel::Extrusion box() {
  using namespace nf::math;
  Region region{{{make_line(Vec2{0, 0}, {10, 0}), make_line(Vec2{10, 0}, {10, 20}),
                  make_line(Vec2{10, 20}, {0, 20}), make_line(Vec2{0, 20}, {0, 0})}},
                {}};
  return {{}, {region}, {0, 0, 30}};
}
Result extrude(nf_native_context* owner, const nf_native_extrusion& input) {
  nf_native_result* result = nullptr;
  REQUIRE(nf_native_extrude(owner, &input, &result) == NF_NATIVE_OK);
  REQUIRE(result);
  return {result, nf_native_result_release};
}
#endif
}  // namespace
TEST_CASE("extrusion ABI layouts and null outputs are deterministic") {
  STATIC_REQUIRE(sizeof(nf_native_result_info) == 136);
  STATIC_REQUIRE(sizeof(nf_native_generated) == 64);
  STATIC_REQUIRE(sizeof(nf_native_curve2) == 56);
  STATIC_REQUIRE(offsetof(nf_native_generated, has_from) == 56);
  nf_native_result* result = nullptr;
  REQUIRE(nf_native_extrude(nullptr, nullptr, &result) == NF_NATIVE_INVALID);
  REQUIRE(result == nullptr);
  REQUIRE(nf_native_extrude(nullptr, nullptr, nullptr) == NF_NATIVE_INVALID);
  nf_native_result_info info{};
  info.volume = 42;
  info.generated_count = 42;
  REQUIRE(nf_native_result_query(nullptr, &info) == NF_NATIVE_INVALID);
  REQUIRE(info.volume == 0);
  REQUIRE(info.generated_count == 0);
  REQUIRE(nf_native_result_query(nullptr, nullptr) == NF_NATIVE_INVALID);
  nf_native_generated generated{};
  generated.kind = 42;
  generated.role_bytes = 42;
  REQUIRE(nf_native_result_generated(nullptr, 0, &generated) == NF_NATIVE_INVALID);
  REQUIRE(generated.kind == 0);
  REQUIRE(generated.role_bytes == 0);
  uint32_t same = 42;
  REQUIRE(nf_native_entities_same(nullptr, nullptr, &same) == NF_NATIVE_INVALID);
  REQUIRE(same == 0);
  REQUIRE(nf_native_entities_same(nullptr, nullptr, nullptr) == NF_NATIVE_INVALID);
  nf_native_result_release(nullptr);
}
TEST_CASE("extrusion backend availability follows context configuration") {
  auto owner = context();
  CHECK(static_cast<bool>(owner) == static_cast<bool>(NF_EXPECT_OCCT));
}
#if NF_EXPECT_OCCT
TEST_CASE("exact native extrusion retains complete measures entities and lineage") {
  auto owner = context();
  const auto source = box();
  Input input(source);
  auto result = extrude(owner.get(), input.value);
  auto other = extrude(owner.get(), input.value);
  owner.reset();  // native results retain their own backend
  nf_native_result_info info{};
  REQUIRE(nf_native_result_query(result.get(), &info) == NF_NATIVE_OK);
  CHECK_THAT(info.volume, WithinAbs(6000, 1e-8));
  CHECK_THAT(info.area, WithinAbs(2200, 1e-8));
  CHECK_THAT(info.centroid[0], WithinAbs(5, 1e-10));
  CHECK_THAT(info.centroid[1], WithinAbs(10, 1e-10));
  CHECK_THAT(info.centroid[2], WithinAbs(15, 1e-10));
  CHECK(info.solids == 1);
  CHECK(info.faces == 6);
  CHECK(info.edges == 12);
  CHECK(info.estimated_bytes > 0);
  CHECK(info.generated_count == 6);
  CHECK(info.history_count == 0);
  std::set<std::tuple<uint64_t, uint64_t, uint64_t>> sources;
  for (uint64_t index = 0; index < info.generated_count; ++index) {
    nf_native_generated face{};
    REQUIRE(nf_native_result_generated(result.get(), index, &face) == NF_NATIVE_OK);
    REQUIRE(face.entity);
    CHECK(face.kind == 1);
    CHECK(face.section == 0);
    std::string role(reinterpret_cast<const char*>(face.role), face.role_bytes);
    CHECK(role == (index == 0 ? "cap_start" : index == 1 ? "cap_end" : "side"));
    CHECK(face.has_from == static_cast<uint32_t>(index >= 2));
    if (face.has_from) sources.emplace(face.region, face.loop, face.curve);
    uint32_t same = 0;
    REQUIRE(nf_native_entities_same(face.entity, face.entity, &same) == NF_NATIVE_OK);
    CHECK(same == 1);
    nf_native_generated foreign{};
    REQUIRE(nf_native_result_generated(other.get(), index, &foreign) == NF_NATIVE_OK);
    REQUIRE(nf_native_entities_same(face.entity, foreign.entity, &same) == NF_NATIVE_OK);
    CHECK(same == 0);
  }
  CHECK(sources.size() == 4);
  nf_native_generated bad{};
  bad.kind = 42;
  CHECK(nf_native_result_generated(result.get(), UINT64_MAX, &bad) == NF_NATIVE_INVALID);
  CHECK(bad.kind == 0);
  // The borrowed source curves are unchanged, including coordinates/weights/knots.
  CHECK(source.regions[0].outer.curves[0].point(0).x == 0);
  CHECK(source.regions[0].outer.curves[0].point(1).x == 10);
}
TEST_CASE("exact extrusion placement and oblique direction match the original backend") {
  auto owner = context();
  auto reference = nf::kernel::make_occt_kernel().value();
  for (int variant = 0; variant < 3; ++variant) {
    auto source = box();
    if (variant == 1) {
      source.plane = {{7, 2, 3}, {0, 1, 0}, {1, 0, 0}};
      source.displacement = {-30, 0, 0};
    }
    if (variant == 2) source.displacement = {4, -7, 30};
    const auto expected = reference->extrude(source).value().shape->measures();
    Input input(source);
    auto result = extrude(owner.get(), input.value);
    nf_native_result_info info{};
    REQUIRE(nf_native_result_query(result.get(), &info) == NF_NATIVE_OK);
    CHECK_THAT(info.volume, WithinAbs(6000, 1e-8));
    CHECK_THAT(info.area, WithinAbs(expected.area, 1e-8));
    CHECK_THAT(info.centroid[0], WithinAbs(expected.centroid.x, 1e-8));
    CHECK_THAT(info.centroid[1], WithinAbs(expected.centroid.y, 1e-8));
    CHECK_THAT(info.centroid[2], WithinAbs(expected.centroid.z, 1e-8));
    CHECK_THAT(info.minimum[0], WithinAbs(expected.minimum.x, 1e-8));
    CHECK_THAT(info.maximum[2], WithinAbs(expected.maximum.z, 1e-8));
    CHECK(info.faces == expected.faces);
    CHECK(info.edges == expected.edges);
  }
}
TEST_CASE("exact extrusion transfers rational circular holes and ellipse profiles") {
  using namespace nf::math;
  auto owner = context();
  auto reference = nf::kernel::make_occt_kernel().value();
  for (int variant = 0; variant < 2; ++variant) {
    auto profiles = variant == 0 ? std::vector<std::vector<NurbsCurve2>>{{make_circle(Vec2{}, 10)},
                                                                         {make_circle(Vec2{}, 4)}}
                                 : std::vector<std::vector<NurbsCurve2>>{
                                       {make_arc(Vec2{}, 5, 3, 0, 0, 2 * std::numbers::pi)}};
    nf::kernel::Extrusion source{{}, regions(profiles, FillRule::kEvenOdd).value(), {0, 0, 5}};
    const auto original = reference->extrude(source).value();
    const auto& expected = original.shape->measures();
    Input input(source);
    auto result = extrude(owner.get(), input.value);
    nf_native_result_info info{};
    REQUIRE(nf_native_result_query(result.get(), &info) == NF_NATIVE_OK);
    CHECK_THAT(info.volume, WithinAbs((variant == 0 ? 420 : 75) * std::numbers::pi, 1e-5));
    CHECK_THAT(info.area, WithinAbs(expected.area, 1e-5));
    CHECK(info.faces == expected.faces);
    CHECK(info.generated_count == expected.faces);
    std::set<uint64_t> loops;
    for (uint64_t index = 0; index < info.generated_count; ++index) {
      nf_native_generated face{};
      REQUIRE(nf_native_result_generated(result.get(), index, &face) == NF_NATIVE_OK);
      const auto& expected_face = original.generated[static_cast<size_t>(index)];
      CHECK(std::string(reinterpret_cast<const char*>(face.role), face.role_bytes) ==
            expected_face.role);
      REQUIRE(face.has_from == static_cast<uint32_t>(expected_face.from.has_value()));
      if (face.has_from) {
        CHECK(face.region == expected_face.from->region);
        CHECK(face.loop == expected_face.from->loop);
        CHECK(face.curve == expected_face.from->curve);
        CHECK(face.section == expected_face.from->section);
        loops.insert(face.loop);
      }
    }
    CHECK(loops.size() == (variant == 0 ? 2 : 1));
  }
}
TEST_CASE("malformed native profile records and rejected geometry publish no result") {
  auto owner = context();
  const auto source = box();
  Input input(source);
  auto reject = [&](uint32_t expected) {
    nf_native_result* result = nullptr;
    CHECK(nf_native_extrude(owner.get(), &input.value, &result) == expected);
    CHECK(result == nullptr);
  };
  SECTION("zero displacement") {
    input.value.displacement[2] = 0;
    reject(NF_NATIVE_FAILURE);
  }
  SECTION("bad plane") {
    input.value.normal[0] = 1;
    reject(NF_NATIVE_FAILURE);
  }
  SECTION("missing region data") {
    input.value.regions = nullptr;
    reject(NF_NATIVE_INVALID);
  }
  SECTION("count overflow") {
    input.value.region_count = SIZE_MAX;
    reject(NF_NATIVE_INVALID);
  }
  SECTION("invalid degree") {
    input.curves[0][0].degree = UINT32_MAX;
    reject(NF_NATIVE_INVALID);
  }
  SECTION("backend knot index overflow") {
    input.curves[0][0].knot_count = static_cast<size_t>(std::numeric_limits<int>::max());
    reject(NF_NATIVE_INVALID);
  }
  SECTION("missing points") {
    input.curves[0][0].points = nullptr;
    reject(NF_NATIVE_INVALID);
  }
  SECTION("point count mismatch before access") {
    input.curves[0][0].point_count = SIZE_MAX;
    reject(NF_NATIVE_INVALID);
  }
  SECTION("wrong weights") {
    input.curves[0][0].weight_count = 1;
    reject(NF_NATIVE_INVALID);
  }
  SECTION("open boundary") {
    input.regions[0].outer.count = 3;
    reject(NF_NATIVE_FAILURE);
  }
  SECTION("misaligned nested pointer") {
    alignas(nf_native_point2) char storage[sizeof(nf_native_point2) + 1]{};
    input.curves[0][0].points = reinterpret_cast<const nf_native_point2*>(storage + 1);
    reject(NF_NATIVE_INVALID);
  }
}
#endif
