// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <limits>
#include <memory>
#include "nf_native.h"
#if NF_EXPECT_OCCT
#include <numbers>
#include "nf/kernel/kernel.hpp"
#include "nf/math/constructors.hpp"
#endif
using Catch::Matchers::WithinAbs;
TEST_CASE("rigid transform history layouts and null boundaries clear outputs") {
  STATIC_REQUIRE(sizeof(nf_native_rigid_transform) == 80);
  STATIC_REQUIRE(sizeof(nf_native_history) == 32);
  STATIC_REQUIRE(sizeof(nf_native_entity_view) == 16);
  STATIC_REQUIRE(offsetof(nf_native_history, before_kind) == 24);
  STATIC_REQUIRE(offsetof(nf_native_rigid_transform, angle) == 72);
  nf_native_result* out = nullptr;
  CHECK(nf_native_transform(nullptr, nullptr, nullptr, &out) == NF_NATIVE_INVALID);
  CHECK(out == nullptr);
  CHECK(nf_native_transform(nullptr, nullptr, nullptr, nullptr) == NF_NATIVE_INVALID);
  nf_native_history history{};
  history.modified_count = 42;
  history.deleted = 42;
  CHECK(nf_native_result_history(nullptr, 0, &history) == NF_NATIVE_INVALID);
  CHECK(history.modified_count == 0);
  CHECK(history.deleted == 0);
  CHECK(nf_native_result_history(nullptr, 0, nullptr) == NF_NATIVE_INVALID);
  nf_native_entity_view entity{};
  entity.kind = 42;
  CHECK(nf_native_result_history_entity(nullptr, 0, 0, 0, &entity) == NF_NATIVE_INVALID);
  CHECK(entity.entity == nullptr);
  CHECK(entity.kind == 0);
  CHECK(nf_native_result_history_entity(nullptr, 0, 0, 0, nullptr) == NF_NATIVE_INVALID);
}
#if NF_EXPECT_OCCT
namespace {
using Context = std::unique_ptr<nf_native_context, decltype(&nf_native_context_release)>;
using Result = std::unique_ptr<nf_native_result, decltype(&nf_native_result_release)>;
Context context() {
  nf_native_context* raw = nullptr;
  REQUIRE(nf_native_context_create(1, 1e-6, 1e-9, &raw) == NF_NATIVE_OK);
  return {raw, nf_native_context_release};
}
Result box(nf_native_context* context) {
  const double knots[] = {0, 0, 1, 1};
  const nf_native_point2 points[][2] = {
      {{0, 0}, {10, 0}}, {{10, 0}, {10, 20}}, {{10, 20}, {0, 20}}, {{0, 20}, {0, 0}}};
  nf_native_curve2 curves[4];
  for (size_t i = 0; i < 4; ++i) curves[i] = {1, knots, 4, points[i], 2, nullptr, 0};
  nf_native_region2 region{{curves, 4}, nullptr, 0};
  nf_native_extrusion input{{0, 0, 0}, {1, 0, 0}, {0, 0, 1}, {0, 0, 30}, &region, 1};
  nf_native_result* out = nullptr;
  REQUIRE(nf_native_extrude(context, &input, &out) == NF_NATIVE_OK);
  return {out, nf_native_result_release};
}
Result moved(nf_native_context* context, const nf_native_result* source,
             const nf_native_rigid_transform& input) {
  nf_native_result* out = nullptr;
  REQUIRE(nf_native_transform(context, source, &input, &out) == NF_NATIVE_OK);
  REQUIRE(out);
  return {out, nf_native_result_release};
}
nf_native_result_info info(const nf_native_result* result) {
  nf_native_result_info out{};
  REQUIRE(nf_native_result_query(result, &out) == NF_NATIVE_OK);
  return out;
}
nf::kernel::Extrusion reference_box() {
  using namespace nf::math;
  Region r{{{make_line(Vec2{0, 0}, {10, 0}), make_line(Vec2{10, 0}, {10, 20}),
             make_line(Vec2{10, 20}, {0, 20}), make_line(Vec2{0, 20}, {0, 0})}},
           {}};
  return {{}, {r}, {0, 0, 30}};
}
}  // namespace
TEST_CASE("native rigid transforms preserve analytic and original kernel measures and history") {
  auto owner = context();
  auto source = box(owner.get());
  auto kernel = nf::kernel::make_occt_kernel().value();
  auto expected_source = kernel->extrude(reference_box()).value();
  for (int variant = 0; variant < 3; ++variant) {
    nf_native_rigid_transform input{{7, 0, 0}, {}, {0, 0, 5}, std::numbers::pi / 2};
    nf::math::Vec3 centroid{-3, 5, 15};
    if (variant == 1) {
      input = {{}, {}, {0, 0, 1}, 0};
      centroid = {5, 10, 15};
    } else if (variant == 2) {
      input = {{7, -2, 4}, {2, 3, 0}, {0, 0, 5}, std::numbers::pi / 2};
      centroid = {2, 4, 19};
    }
    auto result = moved(owner.get(), source.get(), input);
    auto vec = [](const double* p) { return nf::math::Vec3{p[0], p[1], p[2]}; };
    auto reference =
        kernel
            ->transform(expected_source.shape, {vec(input.translation), vec(input.axis_origin),
                                                vec(input.axis_direction), input.angle})
            .value();
    const auto& expected = reference.shape->measures();
    const auto actual = info(result.get());
    CHECK_THAT(actual.volume, WithinAbs(6000, 1e-8));
    CHECK_THAT(actual.area, WithinAbs(2200, 1e-8));
    CHECK_THAT(actual.centroid[0], WithinAbs(centroid.x, 1e-8));
    CHECK_THAT(actual.centroid[1], WithinAbs(centroid.y, 1e-8));
    CHECK_THAT(actual.centroid[2], WithinAbs(centroid.z, 1e-8));
    CHECK_THAT(actual.minimum[0], WithinAbs(expected.minimum.x, 1e-8));
    CHECK_THAT(actual.maximum[1], WithinAbs(expected.maximum.y, 1e-8));
    CHECK(actual.solids == 1);
    CHECK(actual.faces == 6);
    CHECK(actual.edges == 12);
    CHECK(actual.generated_count == 0);
    REQUIRE(actual.history_count == 18);
    CHECK(actual.estimated_bytes > 0);
    size_t faces = 0, edges = 0;
    for (uint64_t i = 0; i < actual.history_count; ++i) {
      nf_native_history change{};
      REQUIRE(nf_native_result_history(result.get(), i, &change) == NF_NATIVE_OK);
      REQUIRE(change.before);
      CHECK(change.modified_count == 1);
      CHECK(change.generated_count == 0);
      CHECK(change.deleted == 0);
      nf_native_entity_view after{};
      REQUIRE(nf_native_result_history_entity(result.get(), i, 0, 0, &after) == NF_NATIVE_OK);
      CHECK(after.kind == change.before_kind);
      if (after.kind == 1)
        ++faces;
      else if (after.kind == 2)
        ++edges;
      else
        FAIL("unexpected topology kind");
      uint32_t same = 42;
      REQUIRE(nf_native_entities_same(change.before, after.entity, &same) == NF_NATIVE_OK);
      CHECK(same == 0);  // distinct snapshots, including an identity transform
      REQUIRE(nf_native_entities_same(change.before, change.before, &same) == NF_NATIVE_OK);
      CHECK(same == 1);
    }
    CHECK(faces == 6);
    CHECK(edges == 12);
    CHECK_THAT(info(source.get()).centroid[0], WithinAbs(5, 1e-10));
    CHECK_THAT(info(source.get()).volume, WithinAbs(6000, 1e-8));
  }
}
TEST_CASE("transform history retains both snapshots after source and context teardown") {
  auto owner = context();
  auto source = box(owner.get());
  auto first = moved(owner.get(), source.get(), {{7, 0, 0}, {}, {0, 0, 1}, std::numbers::pi / 2});
  auto result = moved(owner.get(), first.get(), {{1, 2, 3}, {}, {1, 0, 0}, 0});
  nf_native_generated face{};
  REQUIRE(nf_native_result_generated(source.get(), 0, &face) == NF_NATIVE_OK);
  bool traced = false;
  for (uint64_t i = 0; i < info(first.get()).history_count; ++i) {
    nf_native_history row{};
    REQUIRE(nf_native_result_history(first.get(), i, &row) == NF_NATIVE_OK);
    uint32_t same = 0;
    REQUIRE(nf_native_entities_same(row.before, face.entity, &same) == NF_NATIVE_OK);
    traced |= same != 0;
  }
  CHECK(traced);
  source.reset();
  first.reset();
  owner.reset();
  const auto m = info(result.get());
  CHECK_THAT(m.centroid[0], WithinAbs(-2, 1e-8));
  CHECK_THAT(m.centroid[1], WithinAbs(7, 1e-8));
  CHECK_THAT(m.centroid[2], WithinAbs(18, 1e-8));
  for (uint64_t i = 0; i < m.history_count; ++i) {
    nf_native_history row{};
    REQUIRE(nf_native_result_history(result.get(), i, &row) == NF_NATIVE_OK);
    nf_native_entity_view after{};
    REQUIRE(nf_native_result_history_entity(result.get(), i, 0, 0, &after) == NF_NATIVE_OK);
    uint32_t same = 0;
    CHECK(nf_native_entities_same(row.before, row.before, &same) == NF_NATIVE_OK);
    CHECK(same == 1);
    CHECK(nf_native_entities_same(after.entity, after.entity, &same) == NF_NATIVE_OK);
    CHECK(same == 1);
  }
}
TEST_CASE("native transform failures clear results and preserve the source") {
  auto owner = context();
  auto source = box(owner.get());
  nf_native_rigid_transform input{{}, {}, {0, 0, 1}, 0};
  auto reject = [&](nf_native_context* ctx, const nf_native_rigid_transform* value,
                    uint32_t status) {
    auto* out = source.get();
    CHECK(nf_native_transform(ctx, source.get(), value, &out) == status);
    CHECK(out == nullptr);
  };
  SECTION("foreign context") {
    auto foreign = context();
    reject(foreign.get(), &input, NF_NATIVE_INVALID);
  }
  SECTION("missing context") { reject(nullptr, &input, NF_NATIVE_INVALID); }
  SECTION("missing input") { reject(owner.get(), nullptr, NF_NATIVE_INVALID); }
  SECTION("missing source") {
    auto* out = source.get();
    CHECK(nf_native_transform(owner.get(), nullptr, &input, &out) == NF_NATIVE_INVALID);
    CHECK(out == nullptr);
  }
  SECTION("missing output") {
    CHECK(nf_native_transform(owner.get(), source.get(), &input, nullptr) == NF_NATIVE_INVALID);
  }
  SECTION("misaligned record") {
    alignas(nf_native_rigid_transform) char data[sizeof(input) + 1]{};
    reject(owner.get(), reinterpret_cast<const nf_native_rigid_transform*>(data + 1),
           NF_NATIVE_INVALID);
  }
  SECTION("zero axis even for zero rotation") {
    input.axis_direction[2] = 0;
    reject(owner.get(), &input, NF_NATIVE_FAILURE);
  }
  SECTION("nonfinite fields") {
    for (double bad :
         {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
      for (int field = 0; field < 10; ++field) {
        auto value = input;
        if (field < 3)
          value.translation[field] = bad;
        else if (field < 6)
          value.axis_origin[field - 3] = bad;
        else if (field < 9)
          value.axis_direction[field - 6] = bad;
        else
          value.angle = bad;
        reject(owner.get(), &value, NF_NATIVE_FAILURE);
      }
    }
  }
  CHECK_THAT(info(source.get()).volume, WithinAbs(6000, 1e-8));
  CHECK_THAT(info(source.get()).centroid[0], WithinAbs(5, 1e-10));
}
TEST_CASE("native history indices and list selectors reject without partial views") {
  auto owner = context();
  auto source = box(owner.get());
  auto result = moved(owner.get(), source.get(), {{}, {}, {0, 0, 1}, 0});
  for (uint64_t i : {UINT64_C(18), UINT64_MAX}) {
    nf_native_history row{};
    row.deleted = 42;
    CHECK(nf_native_result_history(result.get(), i, &row) == NF_NATIVE_INVALID);
    CHECK(row.before == nullptr);
    CHECK(row.deleted == 0);
  }
  nf_native_history empty{};
  CHECK(nf_native_result_history(source.get(), 0, &empty) == NF_NATIVE_INVALID);
  CHECK(nf_native_result_history(result.get(), 0, nullptr) == NF_NATIVE_INVALID);
  for (uint32_t list : {UINT32_C(0), UINT32_C(1), UINT32_MAX}) {
    for (uint64_t index : {UINT64_C(1), UINT64_MAX}) {
      nf_native_entity_view view{};
      view.kind = 42;
      CHECK(nf_native_result_history_entity(result.get(), 0, list, index, &view) ==
            NF_NATIVE_INVALID);
      CHECK(view.entity == nullptr);
      CHECK(view.kind == 0);
    }
  }
  nf_native_entity_view view{};
  CHECK(nf_native_result_history_entity(result.get(), UINT64_MAX, 0, 0, &view) ==
        NF_NATIVE_INVALID);
  CHECK(nf_native_result_history_entity(result.get(), 0, 1, 0, &view) == NF_NATIVE_INVALID);
  CHECK(nf_native_result_history_entity(result.get(), 0, 2, 0, &view) == NF_NATIVE_INVALID);
  CHECK(nf_native_result_history_entity(result.get(), 0, 0, 0, nullptr) == NF_NATIVE_INVALID);
}
#endif
