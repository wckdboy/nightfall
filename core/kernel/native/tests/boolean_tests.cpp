// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <limits>
#include <memory>
#include <vector>
#include "result.hpp"
#if NF_EXPECT_OCCT
#include <BRepCheck_Analyzer.hxx>
#include <BRepTools.hxx>
#include <numbers>
#include <sstream>
#include "snapshot.hpp"
#endif
using Catch::Matchers::WithinAbs;
TEST_CASE("boolean ABI modes and null boundaries are stable") {
  STATIC_REQUIRE(NF_NATIVE_UNION == 0);
  STATIC_REQUIRE(NF_NATIVE_SUBTRACT == 1);
  STATIC_REQUIRE(NF_NATIVE_INTERSECT == 2);
  nf_native_result* out = nullptr;
  CHECK(nf_native_boolean(nullptr, nullptr, nullptr, 0, NF_NATIVE_UNION, &out) ==
        NF_NATIVE_INVALID);
  CHECK(out == nullptr);
  CHECK(nf_native_boolean(nullptr, nullptr, nullptr, 0, 0, nullptr) == NF_NATIVE_INVALID);
  alignas(nf_native_result*) std::array<std::byte, sizeof(out) + 1> slot;
  slot.fill(std::byte{0x7f});
  const auto before = slot;
  CHECK(nf_native_boolean(nullptr, nullptr, nullptr, 0, 0,
                          reinterpret_cast<nf_native_result**>(slot.data() + 1)) ==
        NF_NATIVE_INVALID);
  CHECK(slot == before);
}
#if NF_EXPECT_OCCT
namespace {
using Context = std::unique_ptr<nf_native_context, decltype(&nf_native_context_release)>;
using Result = std::unique_ptr<nf_native_result, decltype(&nf_native_result_release)>;
Context context() {
  nf_native_context* out = nullptr;
  REQUIRE(nf_native_context_create(1, 1e-6, 1e-9, &out) == NF_NATIVE_OK);
  return {out, nf_native_context_release};
}
Result box(nf_native_context* context, std::array<double, 3> origin = {},
           std::array<double, 3> size = {10, 10, 10}) {
  const double knots[] = {0, 0, 1, 1};
  const nf_native_point2 points[][2] = {{{0, 0}, {size[0], 0}},
                                        {{size[0], 0}, {size[0], size[1]}},
                                        {{size[0], size[1]}, {0, size[1]}},
                                        {{0, size[1]}, {0, 0}}};
  nf_native_curve2 curves[4];
  for (size_t i = 0; i < 4; ++i) curves[i] = {1, knots, 4, points[i], 2, nullptr, 0};
  const nf_native_region2 region{{curves, 4}, nullptr, 0};
  const nf_native_extrusion input{
      {origin[0], origin[1], origin[2]}, {1, 0, 0}, {0, 0, 1}, {0, 0, size[2]}, &region, 1};
  nf_native_result* out = nullptr;
  REQUIRE(nf_native_extrude(context, &input, &out) == NF_NATIVE_OK);
  return {out, nf_native_result_release};
}
Result operation(nf_native_context* context, const nf_native_result* target,
                 const std::vector<const nf_native_result*>& tools, uint32_t mode) {
  nf_native_result* out = nullptr;
  REQUIRE(nf_native_boolean(context, target, tools.data(), tools.size(), mode, &out) ==
          NF_NATIVE_OK);
  REQUIRE(out);
  return {out, nf_native_result_release};
}
std::string serialized(const nf_native_result* result) {
  std::ostringstream stream;
  BRepTools::Write(
      std::dynamic_pointer_cast<const nf::kernel::detail::Snapshot>(result->operation.shape)
          ->geometry,
      stream);
  return stream.str();
}
void inspect(const nf_native_result* result, double volume, double area, uint64_t solids,
             uint64_t rows, uint64_t faces, uint64_t edges) {
  nf_native_result_info m{};
  REQUIRE(nf_native_result_query(result, &m) == NF_NATIVE_OK);
  CHECK_THAT(m.volume, WithinAbs(volume, 1e-7));
  CHECK_THAT(m.area, WithinAbs(area, 1e-7));
  CHECK(m.solids == solids);
  CHECK(m.faces == faces);
  CHECK(m.edges == edges);
  CHECK(m.history_count == rows);
  CHECK(m.estimated_bytes > 0);
  const auto geometry =
      std::dynamic_pointer_cast<const nf::kernel::detail::Snapshot>(result->operation.shape)
          ->geometry;
  CHECK(BRepCheck_Analyzer(geometry).IsValid());
  REQUIRE(nf::kernel::detail::snapshot(geometry));
  for (uint64_t i = 0; i < rows; ++i) {
    nf_native_history row{};
    REQUIRE(nf_native_result_history(result, i, &row) == NF_NATIVE_OK);
    REQUIRE(row.before);
    CHECK((row.before_kind == 1 || row.before_kind == 2));
    CHECK(row.deleted == static_cast<uint32_t>(row.modified_count == 0));
    uint32_t same = 0;
    REQUIRE(nf_native_entities_same(row.before, row.before, &same) == NF_NATIVE_OK);
    CHECK(same == 1);
    for (uint32_t list = 0; list < 2; ++list) {
      for (uint64_t j = 0; j < (list == 0 ? row.modified_count : row.generated_count); ++j) {
        nf_native_entity_view entity{};
        REQUIRE(nf_native_result_history_entity(result, i, list, j, &entity) == NF_NATIVE_OK);
        REQUIRE(entity.entity);
        CHECK((entity.kind == 1 || entity.kind == 2));
        REQUIRE(nf_native_entities_same(entity.entity, entity.entity, &same) == NF_NATIVE_OK);
        CHECK(same == 1);
      }
    }
  }
}
}  // namespace
TEST_CASE("native boolean modes preserve exact box goldens source bytes and complete history") {
  auto owner = context();
  auto target = box(owner.get()), tool = box(owner.get(), {5, 0, 0});
  const auto saved_target = serialized(target.get()), saved_tool = serialized(tool.get());
  for (uint32_t mode : {NF_NATIVE_UNION, NF_NATIVE_SUBTRACT, NF_NATIVE_INTERSECT}) {
    auto result = operation(owner.get(), target.get(), {tool.get()}, mode);
    inspect(result.get(), mode == NF_NATIVE_UNION ? 1500 : 500, mode == NF_NATIVE_UNION ? 800 : 400,
            1, 36, 6, 12);
    CHECK(serialized(target.get()) == saved_target);
    CHECK(serialized(tool.get()) == saved_tool);
  }
}
TEST_CASE("native boolean tools form a union with split solids cavities and duplicate history") {
  auto owner = context();
  auto target = box(owner.get());
  auto first = box(owner.get(), {2, -1, -1}, {2, 12, 12});
  auto second = box(owner.get(), {6, -1, -1}, {2, 12, 12});
  auto cut = operation(owner.get(), target.get(), {first.get(), second.get()}, NF_NATIVE_SUBTRACT);
  inspect(cut.get(), 600, 840, 3, 54, 18, 36);
  auto common =
      operation(owner.get(), target.get(), {first.get(), second.get()}, NF_NATIVE_INTERSECT);
  inspect(common.get(), 400, 560, 2, 54, 12, 24);
  auto repeated =
      operation(owner.get(), target.get(), {first.get(), first.get()}, NF_NATIVE_SUBTRACT);
  inspect(repeated.get(), 800, 720, 2, 36, 12, 24);
  auto inner = box(owner.get(), {2, 2, 2}, {6, 6, 6});
  auto cavity = operation(owner.get(), target.get(), {inner.get()}, NF_NATIVE_SUBTRACT);
  inspect(cavity.get(), 784, 816, 1, 36, 12, 24);
}
TEST_CASE("native boolean retained readers survive all inputs context and intermediate results") {
  auto owner = context();
  auto target = box(owner.get()), distant = box(owner.get(), {20, 0, 0});
  auto result = operation(owner.get(), target.get(), {distant.get()}, NF_NATIVE_UNION);
  auto repeated =
      operation(owner.get(), target.get(), {target.get(), target.get()}, NF_NATIVE_UNION);
  inspect(repeated.get(), 1000, 600, 1, 18, 6, 12);
  repeated.reset();
  target.reset();
  distant.reset();
  owner.reset();
  inspect(result.get(), 2000, 1200, 2, 36, 12, 24);
}
TEST_CASE("native boolean rejects malformed foreign and oversized lists before geometry access") {
  auto owner = context(), foreign = context();
  auto target = box(owner.get()), tool = box(owner.get(), {5, 0, 0});
  auto other = box(foreign.get());
  nf_native_result malformed{owner->kernel, {}};
  const auto before = serialized(target.get());
  for (const auto& tools : {std::vector<const nf_native_result*>{},
                            {nullptr},
                            {other.get()},
                            {tool.get(), other.get()},
                            {other.get(), tool.get()},
                            {&malformed}}) {
    auto* out = target.get();
    CHECK(nf_native_boolean(owner.get(), target.get(), tools.data(), tools.size(), NF_NATIVE_UNION,
                            &out) == NF_NATIVE_INVALID);
    CHECK(out == nullptr);
  }
  const nf_native_result* tools[] = {tool.get()};
  for (uint32_t mode : {UINT32_C(3), UINT32_MAX}) {
    auto* out = target.get();
    CHECK(nf_native_boolean(owner.get(), target.get(), tools, 1, mode, &out) == NF_NATIVE_INVALID);
    CHECK(out == nullptr);
  }
  nf_native_result* out = nullptr;
  CHECK(nf_native_boolean(owner.get(), target.get(), tools, UINT64_MAX, NF_NATIVE_UNION, &out) ==
        NF_NATIVE_INVALID);
  CHECK(out == nullptr);
  CHECK(nf_native_boolean(owner.get(), other.get(), tools, 1, NF_NATIVE_UNION, &out) ==
        NF_NATIVE_INVALID);
  CHECK(nf_native_boolean(owner.get(), nullptr, tools, 1, NF_NATIVE_UNION, &out) ==
        NF_NATIVE_INVALID);
  CHECK(nf_native_boolean(owner.get(), target.get(), nullptr, 1, NF_NATIVE_UNION, &out) ==
        NF_NATIVE_INVALID);
  alignas(nf_native_result*) std::array<std::byte, sizeof(tools) + 1> data{};
  CHECK(nf_native_boolean(owner.get(), target.get(),
                          reinterpret_cast<const nf_native_result* const*>(data.data() + 1), 1,
                          NF_NATIVE_UNION, &out) == NF_NATIVE_INVALID);
  CHECK(out == nullptr);
  auto recovered = operation(owner.get(), target.get(), {tool.get()}, NF_NATIVE_UNION);
  inspect(recovered.get(), 1500, 800, 1, 36, 6, 12);
  CHECK(serialized(target.get()) == before);
}
TEST_CASE("native boolean empty non-solid and solver failures publish nothing and recover") {
  auto owner = context();
  auto target = box(owner.get()), distant = box(owner.get(), {20, 0, 0});
  auto touching = box(owner.get(), {10, 0, 0});
  const auto before = serialized(target.get());
  nf_native_result invalid{owner->kernel,
                           {.shape = std::make_shared<nf::kernel::detail::Snapshot>()}};
  for (const auto* tool : {distant.get(), touching.get(), &invalid}) {
    const nf_native_result* tools[] = {tool};
    auto* out = target.get();
    CHECK(nf_native_boolean(owner.get(), target.get(), tools, 1, NF_NATIVE_INTERSECT, &out) ==
          NF_NATIVE_FAILURE);
    CHECK(out == nullptr);
  }
  const nf_native_result* tools[] = {target.get()};
  nf_native_result* out = nullptr;
  CHECK(nf_native_boolean(owner.get(), target.get(), tools, 1, NF_NATIVE_SUBTRACT, &out) ==
        NF_NATIVE_FAILURE);
  CHECK(out == nullptr);
  auto recovered = operation(owner.get(), target.get(), {distant.get()}, NF_NATIVE_SUBTRACT);
  inspect(recovered.get(), 1000, 600, 1, 36, 6, 12);
  CHECK(serialized(target.get()) == before);
}
#else
TEST_CASE("boolean boundary reports unavailable for valid opaque owners") {
  nf_native_context context;
  nf_native_result target, tool;
  const nf_native_result* tools[] = {&tool};
  nf_native_result* out = nullptr;
  CHECK(nf_native_boolean(&context, &target, tools, 1, NF_NATIVE_UNION, &out) ==
        NF_NATIVE_UNAVAILABLE);
  CHECK(out == nullptr);
}
#endif
