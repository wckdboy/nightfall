// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include "result.hpp"
#if NF_EXPECT_OCCT
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <GProp_GProps.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Reader.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <cmath>
#include <numbers>
#include <sstream>
#include "snapshot.hpp"
#endif
using Catch::Matchers::WithinAbs;
TEST_CASE("STEP ABI layout and null boundaries clear valid outputs") {
  STATIC_REQUIRE(sizeof(nf_native_step_info) == 16);
  STATIC_REQUIRE(alignof(nf_native_step_info) == 8);
  STATIC_REQUIRE(offsetof(nf_native_step_info, byte_count) == 8);
  nf_native_step* out = nullptr;
  CHECK(nf_native_export_step(nullptr, nullptr, 0, &out) == NF_NATIVE_INVALID);
  CHECK(out == nullptr);
  CHECK(nf_native_export_step(nullptr, nullptr, 0, nullptr) == NF_NATIVE_INVALID);
  nf_native_step_info info{nullptr, 42};
  CHECK(nf_native_step_query(nullptr, &info) == NF_NATIVE_INVALID);
  CHECK(info.bytes == nullptr);
  CHECK(info.byte_count == 0);
  CHECK(nf_native_step_query(nullptr, nullptr) == NF_NATIVE_INVALID);
  nf_native_step_release(nullptr);
}
TEST_CASE("STEP rejects misaligned output records without touching storage") {
  alignas(nf_native_step*) std::array<std::byte, sizeof(nf_native_step*) + 1> owner;
  owner.fill(std::byte{0x7f});
  const auto original_owner = owner;
  CHECK(nf_native_export_step(nullptr, nullptr, 0,
                              reinterpret_cast<nf_native_step**>(owner.data() + 1)) ==
        NF_NATIVE_INVALID);
  CHECK(owner == original_owner);
  alignas(nf_native_step_info) std::array<std::byte, sizeof(nf_native_step_info) + 1> info;
  info.fill(std::byte{0x7f});
  const auto original_info = info;
  CHECK(nf_native_step_query(nullptr, reinterpret_cast<nf_native_step_info*>(info.data() + 1)) ==
        NF_NATIVE_INVALID);
  CHECK(info == original_info);
}
#if NF_EXPECT_OCCT
namespace {
using Context = std::unique_ptr<nf_native_context, decltype(&nf_native_context_release)>;
using Result = std::unique_ptr<nf_native_result, decltype(&nf_native_result_release)>;
using Step = std::unique_ptr<nf_native_step, decltype(&nf_native_step_release)>;
Context context() {
  nf_native_context* out = nullptr;
  REQUIRE(nf_native_context_create(1, 1e-6, 1e-9, &out) == NF_NATIVE_OK);
  return {out, nf_native_context_release};
}
Result solid(nf_native_context* context, bool ring = false) {
  const double line_knots[] = {0, 0, 1, 1};
  const nf_native_point2 corners[][2] = {
      {{0, 0}, {10, 0}}, {{10, 0}, {10, 20}}, {{10, 20}, {0, 20}}, {{0, 20}, {0, 0}}};
  nf_native_curve2 lines[4];
  for (size_t i = 0; i < 4; ++i) lines[i] = {1, line_knots, 4, corners[i], 2, nullptr, 0};
  const double knots[] = {0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 4};
  const double w = std::sqrt(0.5);
  const double weights[] = {1, w, 1, w, 1, w, 1, w, 1};
  const nf_native_point2 outer[] = {{10, 0},    {10, 10}, {0, 10},   {-10, 10}, {-10, 0},
                                    {-10, -10}, {0, -10}, {10, -10}, {10, 0}};
  const nf_native_point2 inner[] = {{5, 0},  {5, -5}, {0, -5}, {-5, -5}, {-5, 0},
                                    {-5, 5}, {0, 5},  {5, 5},  {5, 0}};
  const nf_native_curve2 circles[] = {{2, knots, 12, outer, 9, weights, 9},
                                      {2, knots, 12, inner, 9, weights, 9}};
  const nf_native_loop2 hole{circles + 1, 1};
  const nf_native_region2 region =
      ring ? nf_native_region2{{circles, 1}, &hole, 1} : nf_native_region2{{lines, 4}, nullptr, 0};
  const nf_native_extrusion input{{}, {1, 0, 0}, {0, 0, 1}, {0, 0, 30}, &region, 1};
  nf_native_result* out = nullptr;
  REQUIRE(nf_native_extrude(context, &input, &out) == NF_NATIVE_OK);
  return {out, nf_native_result_release};
}
Step export_step(nf_native_context* context, const std::vector<const nf_native_result*>& sources) {
  nf_native_step* out = nullptr;
  REQUIRE(nf_native_export_step(context, sources.data(), sources.size(), &out) == NF_NATIVE_OK);
  REQUIRE(out);
  return {out, nf_native_step_release};
}
nf_native_step_info query(const nf_native_step* step) {
  nf_native_step_info info{};
  REQUIRE(nf_native_step_query(step, &info) == NF_NATIVE_OK);
  REQUIRE(info.bytes);
  REQUIRE(info.byte_count > 0);
  REQUIRE(info.byte_count <= static_cast<uint64_t>(std::numeric_limits<std::ptrdiff_t>::max()));
  return info;
}
std::string text(const nf_native_step* step) {
  const auto info = query(step);
  return {reinterpret_cast<const char*>(info.bytes), static_cast<size_t>(info.byte_count)};
}
std::string serialized(const nf::kernel::ShapePtr& shape) {
  std::ostringstream stream;
  BRepTools::Write(std::dynamic_pointer_cast<const nf::kernel::detail::Snapshot>(shape)->geometry,
                   stream);
  return stream.str();
}
void reload(const std::string& bytes, double volume, double area, nf::math::Vec3 center,
            size_t solids = 1, size_t faces = 6, size_t edges = 12) {
  CHECK(bytes.starts_with("ISO-10303-21;"));
  CHECK(bytes.find("AP242") != std::string::npos);
  CHECK(bytes.find("TESSELLATED") == std::string::npos);
  std::istringstream stream(bytes);
  STEPControl_Reader reader;
  REQUIRE(reader.ReadStream("native-round-trip.step", stream) == IFSelect_RetDone);
  reader.SetSystemLengthUnit(1);
  REQUIRE(reader.TransferRoots() > 0);
  const auto shape = reader.OneShape();
  REQUIRE(BRepCheck_Analyzer(shape).IsValid());
  for (const auto& [kind, count] :
       {std::pair{TopAbs_SOLID, solids}, {TopAbs_FACE, faces}, {TopAbs_EDGE, edges}}) {
    TopTools_IndexedMapOfShape found;
    TopExp::MapShapes(shape, kind, found);
    CHECK(static_cast<size_t>(found.Extent()) == count);
  }
  GProp_GProps mass, surface;
  BRepGProp::VolumeProperties(shape, mass);
  BRepGProp::SurfaceProperties(shape, surface);
  CHECK_THAT(mass.Mass(), WithinAbs(volume, 1e-6));
  CHECK_THAT(surface.Mass(), WithinAbs(area, 1e-6));
  // GProps::Add drops first moments at zero partial signed sums. Use Nightfall's
  // established closed-solid validator/conditioned shell integrator for the centroid.
  const auto validated = nf::kernel::detail::snapshot(shape);
  REQUIRE(validated);
  const auto& measured = (*validated)->measures();
  CHECK_THAT(measured.volume, WithinAbs(volume, 1e-6));
  CHECK_THAT(measured.area, WithinAbs(area, 1e-6));
  CHECK_THAT(measured.centroid.x, WithinAbs(center.x, 1e-7));
  CHECK_THAT(measured.centroid.y, WithinAbs(center.y, 1e-7));
  CHECK_THAT(measured.centroid.z, WithinAbs(center.z, 1e-7));
}
}  // namespace
TEST_CASE("native STEP bytes round trip exact geometry and preserve source faces") {
  auto owner = context();
  auto source = solid(owner.get());
  const auto before = serialized(source->operation.shape);
  nf_native_generated face{};
  REQUIRE(nf_native_result_generated(source.get(), 2, &face) == NF_NATIVE_OK);
  auto first = export_step(owner.get(), {source.get()});
  const auto info = query(first.get());
  const auto bytes = text(first.get());
  reload(bytes, 6000, 2200, {5, 10, 15});
  auto second = export_step(owner.get(), {source.get(), source.get()});
  reload(text(second.get()), 12000, 4400, {5, 10, 15}, 2, 12, 24);
  second.reset();
  CHECK(query(first.get()).bytes == info.bytes);
  CHECK(text(first.get()) == bytes);
  CHECK(serialized(source->operation.shape) == before);
  nf_native_generated after{};
  REQUIRE(nf_native_result_generated(source.get(), 2, &after) == NF_NATIVE_OK);
  uint32_t same = 0;
  REQUIRE(nf_native_entities_same(face.entity, after.entity, &same) == NF_NATIVE_OK);
  CHECK(same == 1);
}
TEST_CASE("native STEP output outlives rigid source results and context") {
  auto owner = context();
  auto source = solid(owner.get());
  const nf_native_rigid_transform transform{{7, 2, 3}, {2, 1, 3}, {0, 0, 5}, std::numbers::pi / 2};
  nf_native_result* raw = nullptr;
  REQUIRE(nf_native_transform(owner.get(), source.get(), &transform, &raw) == NF_NATIVE_OK);
  Result placed(raw, nf_native_result_release);
  auto output = export_step(owner.get(), {placed.get()});
  const auto info = query(output.get());
  const auto bytes = text(output.get());
  source.reset();
  placed.reset();
  owner.reset();
  CHECK(query(output.get()).bytes == info.bytes);
  CHECK(text(output.get()) == bytes);
  reload(bytes, 6000, 2200, {0, 6, 18});
}
TEST_CASE("native STEP keeps exact rational holes and analytic cylinders") {
  auto owner = context();
  auto source = solid(owner.get(), true);
  const auto before = serialized(source->operation.shape);
  auto output = export_step(owner.get(), {source.get()});
  const auto bytes = text(output.get());
  CHECK(bytes.find("CYLINDRICAL_SURFACE") != std::string::npos);
  const auto& m = source->operation.shape->measures();
  reload(bytes, 2250 * std::numbers::pi, 1050 * std::numbers::pi, {0, 0, 15}, 1, m.faces, m.edges);
  CHECK(serialized(source->operation.shape) == before);
}
TEST_CASE("native STEP preserves separated inputs and multiple solids in one snapshot") {
  auto owner = context();
  auto source = solid(owner.get());
  const nf_native_rigid_transform transform{{20, 0, 0}, {}, {0, 0, 1}, 0};
  nf_native_result* raw = nullptr;
  REQUIRE(nf_native_transform(owner.get(), source.get(), &transform, &raw) == NF_NATIVE_OK);
  Result moved(raw, nf_native_result_release);
  SECTION("separate inputs") {
    auto output = export_step(owner.get(), {source.get(), moved.get()});
    reload(text(output.get()), 12000, 4400, {15, 10, 15}, 2, 12, 24);
  }
  SECTION("compound snapshot") {
    auto operation = owner->kernel->boolean_operation(
        source->operation.shape, {moved->operation.shape}, nf::kernel::BooleanOp::kUnion);
    REQUIRE(operation);
    Result compound(new nf_native_result{owner->kernel, std::move(*operation)},
                    nf_native_result_release);
    auto output = export_step(owner.get(), {compound.get()});
    reload(text(output.get()), 12000, 4400, {15, 10, 15}, 2, 12, 24);
  }
}
TEST_CASE("native STEP rejects invalid foreign and oversized inputs and recovers") {
  auto owner = context();
  auto source = solid(owner.get());
  auto foreign = context();
  auto foreign_source = solid(foreign.get());
  const auto before = serialized(source->operation.shape);
  nf_native_result malformed{owner->kernel, {}};
  for (const auto& inputs : {std::vector<const nf_native_result*>{},
                             {nullptr},
                             {source.get(), nullptr},
                             {foreign_source.get()},
                             {source.get(), foreign_source.get()},
                             {&malformed}}) {
    nf_native_step* output = nullptr;
    CHECK(nf_native_export_step(owner.get(), inputs.data(), inputs.size(), &output) ==
          NF_NATIVE_INVALID);
    CHECK(output == nullptr);
  }
  const nf_native_result* sources[] = {source.get()};
  nf_native_step* output = nullptr;
  CHECK(nf_native_export_step(owner.get(), sources, UINT64_MAX, &output) == NF_NATIVE_INVALID);
  CHECK(output == nullptr);
  alignas(nf_native_result*) std::array<std::byte, sizeof(sources) + 1> misaligned{};
  CHECK(nf_native_export_step(
            owner.get(), reinterpret_cast<const nf_native_result* const*>(misaligned.data() + 1), 1,
            &output) == NF_NATIVE_INVALID);
  CHECK(output == nullptr);
  CHECK(nf_native_export_step(owner.get(), nullptr, 1, &output) == NF_NATIVE_INVALID);
  CHECK(output == nullptr);
  auto recovered = export_step(owner.get(), {source.get()});
  reload(text(recovered.get()), 6000, 2200, {5, 10, 15});
  CHECK(serialized(source->operation.shape) == before);
}
TEST_CASE("native STEP solver failures clear outputs and preserve prior retained bytes") {
  auto owner = context();
  auto source = solid(owner.get());
  const auto before = serialized(source->operation.shape);
  auto retained = export_step(owner.get(), {source.get()});
  const auto bytes = text(retained.get());
  nf_native_result invalid{owner->kernel,
                           {.shape = std::make_shared<nf::kernel::detail::Snapshot>()}};
  const nf_native_result* inputs[] = {source.get(), &invalid};
  // This slot borrows an already-owned allocation; failure must clear it without freeing it.
  nf_native_step* output = retained.get();
  CHECK(nf_native_export_step(owner.get(), inputs, 2, &output) == NF_NATIVE_FAILURE);
  CHECK(output == nullptr);
  nf_native_error_info diagnostic{};
  REQUIRE(nf_native_last_error(&diagnostic) == NF_NATIVE_OK);
  CHECK(std::string(reinterpret_cast<const char*>(diagnostic.code), diagnostic.code_bytes) ==
        "kernel");
  CHECK(diagnostic.message_bytes > 0);
  CHECK(text(retained.get()) == bytes);
  auto foreign = context();
  auto foreign_source = solid(foreign.get());
  const nf_native_result* mixed[] = {&invalid, foreign_source.get()};
  CHECK(nf_native_export_step(owner.get(), mixed, 2, &output) == NF_NATIVE_INVALID);
  CHECK(output == nullptr);
  auto recovered = export_step(owner.get(), {source.get()});
  reload(text(recovered.get()), 6000, 2200, {5, 10, 15});
  CHECK(serialized(source->operation.shape) == before);
}
#else
TEST_CASE("STEP boundary reports unavailable for valid opaque owners") {
  nf_native_context context;
  nf_native_result source;
  const nf_native_result* sources[] = {&source};
  nf_native_step* out = nullptr;
  CHECK(nf_native_export_step(&context, sources, 1, &out) == NF_NATIVE_UNAVAILABLE);
  CHECK(out == nullptr);
}
#endif
