// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <vector>
#include "nf_native.h"
#if NF_EXPECT_OCCT
#include "nf/math/vec.hpp"
#endif
using Catch::Matchers::WithinAbs;
TEST_CASE("mesh ABI layouts and null boundaries clear borrowed outputs") {
  STATIC_REQUIRE(sizeof(nf_native_meshing_options) == 24);
  STATIC_REQUIRE(sizeof(nf_native_mesh_vertex) == 48);
  STATIC_REQUIRE(sizeof(nf_native_mesh_info) == 40);
  STATIC_REQUIRE(sizeof(nf_native_mesh_face) == 16);
  STATIC_REQUIRE(offsetof(nf_native_mesh_info, vertex_count) == 16);
  STATIC_REQUIRE(offsetof(nf_native_mesh_face, first_index) == 8);
  nf_native_mesh* mesh = nullptr;
  CHECK(nf_native_tessellate(nullptr, nullptr, nullptr, &mesh) == NF_NATIVE_INVALID);
  CHECK(mesh == nullptr);
  CHECK(nf_native_tessellate(nullptr, nullptr, nullptr, nullptr) == NF_NATIVE_INVALID);
  nf_native_mesh_info info{};
  info.vertex_count = 42;
  CHECK(nf_native_mesh_query(nullptr, &info) == NF_NATIVE_INVALID);
  CHECK(info.vertex_count == 0);
  CHECK(info.vertices == nullptr);
  CHECK(info.indices == nullptr);
  CHECK(nf_native_mesh_query(nullptr, nullptr) == NF_NATIVE_INVALID);
  nf_native_mesh_face face{};
  face.index_count = 42;
  CHECK(nf_native_mesh_face_at(nullptr, 0, &face) == NF_NATIVE_INVALID);
  CHECK(face.face == nullptr);
  CHECK(face.index_count == 0);
  CHECK(nf_native_mesh_face_at(nullptr, 0, nullptr) == NF_NATIVE_INVALID);
  nf_native_mesh_release(nullptr);
}
#if NF_EXPECT_OCCT
namespace {
using Context = std::unique_ptr<nf_native_context, decltype(&nf_native_context_release)>;
using Result = std::unique_ptr<nf_native_result, decltype(&nf_native_result_release)>;
using Mesh = std::unique_ptr<nf_native_mesh, decltype(&nf_native_mesh_release)>;
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
Mesh tessellate(nf_native_context* ctx, const nf_native_result* source,
                nf_native_meshing_options options = {0.1, 0.2, 1'000'000}) {
  nf_native_mesh* out = nullptr;
  REQUIRE(nf_native_tessellate(ctx, source, &options, &out) == NF_NATIVE_OK);
  REQUIRE(out);
  return {out, nf_native_mesh_release};
}
nf_native_mesh_info query(const nf_native_mesh* mesh) {
  nf_native_mesh_info out{};
  REQUIRE(nf_native_mesh_query(mesh, &out) == NF_NATIVE_OK);
  REQUIRE(out.vertices);
  REQUIRE(out.indices);
  return out;
}
struct Approximation {
  double volume = 0, area = 0;
};
Approximation inspect(const nf_native_mesh* mesh, const nf_native_result* source = nullptr) {
  using namespace nf::math;
  const auto info = query(mesh);
  REQUIRE(info.vertex_count > 0);
  REQUIRE(info.vertex_count <= 3'000'000);
  REQUIRE(info.index_count > 0);
  REQUIRE(info.index_count % 3 == 0);
  REQUIRE(info.index_count <= 3'000'000);
  uint64_t offset = 0;
  for (uint64_t f = 0; f < info.face_count; ++f) {
    nf_native_mesh_face face{};
    REQUIRE(nf_native_mesh_face_at(mesh, f, &face) == NF_NATIVE_OK);
    REQUIRE(face.face);
    CHECK(face.first_index == offset);
    CHECK(face.index_count > 0);
    CHECK(face.index_count % 3 == 0);
    offset += face.index_count;
    REQUIRE(offset <= info.index_count);
    uint32_t same = 0;
    CHECK(nf_native_entities_same(face.face, face.face, &same) == NF_NATIVE_OK);
    CHECK(same == 1);
    if (source) {
      nf_native_result_info measures{};
      REQUIRE(nf_native_result_query(source, &measures) == NF_NATIVE_OK);
      CHECK(info.face_count == measures.faces);
      size_t matches = 0;
      for (uint64_t i = 0; i < measures.generated_count; ++i) {
        nf_native_generated generated{};
        REQUIRE(nf_native_result_generated(source, i, &generated) == NF_NATIVE_OK);
        REQUIRE(nf_native_entities_same(face.face, generated.entity, &same) == NF_NATIVE_OK);
        matches += same;
      }
      CHECK(matches == 1);
    }
    for (uint64_t other = 0; other < f; ++other) {
      nf_native_mesh_face prior{};
      REQUIRE(nf_native_mesh_face_at(mesh, other, &prior) == NF_NATIVE_OK);
      REQUIRE(nf_native_entities_same(face.face, prior.face, &same) == NF_NATIVE_OK);
      CHECK(same == 0);
    }
  }
  CHECK(offset == info.index_count);
  auto vec = [](const double* p) { return Vec3{p[0], p[1], p[2]}; };
  for (uint64_t i = 0; i < info.vertex_count; ++i) {
    const auto& v = info.vertices[i];
    for (int axis = 0; axis < 3; ++axis) {
      CHECK(std::isfinite(v.position[axis]));
      CHECK(std::isfinite(v.normal[axis]));
    }
    CHECK_THAT(length(vec(v.normal)), WithinAbs(1, 1e-6));
  }
  Approximation result;
  for (uint64_t i = 0; i < info.index_count; i += 3) {
    for (uint64_t j = 0; j < 3; ++j) REQUIRE(info.indices[i + j] < info.vertex_count);
    const auto& a = info.vertices[info.indices[i]];
    const auto& b = info.vertices[info.indices[i + 1]];
    const auto& c = info.vertices[info.indices[i + 2]];
    const auto cross_product =
        cross(vec(b.position) - vec(a.position), vec(c.position) - vec(a.position));
    CHECK(length(cross_product) > 0);
    CHECK(dot(cross_product, vec(a.normal) + vec(b.normal) + vec(c.normal)) > 0);
    result.area += length(cross_product) / 2;
    result.volume += dot(vec(a.position), cross(vec(b.position), vec(c.position))) / 6;
  }
  return result;
}
}  // namespace
TEST_CASE("native mesh buffers retain analytic box geometry and original picking faces") {
  auto owner = context();
  auto source = solid(owner.get());
  auto mesh = tessellate(owner.get(), source.get());
  const auto buffers = query(mesh.get());
  CHECK(buffers.vertex_count == 24);
  CHECK(buffers.index_count == 36);
  CHECK(buffers.face_count == 6);
  const auto exact = inspect(mesh.get(), source.get());
  CHECK_THAT(exact.volume, WithinAbs(6000, 1e-7));
  CHECK_THAT(exact.area, WithinAbs(2200, 1e-7));
  source.reset();
  owner.reset();
  CHECK(query(mesh.get()).vertices == buffers.vertices);
  CHECK(query(mesh.get()).indices == buffers.indices);
  const auto retained = inspect(mesh.get());
  CHECK_THAT(retained.volume, WithinAbs(6000, 1e-7));
  CHECK_THAT(retained.area, WithinAbs(2200, 1e-7));
}
TEST_CASE("native meshes carry rotated world positions and outward normals") {
  auto owner = context();
  auto source = solid(owner.get());
  nf_native_result* raw = nullptr;
  const nf_native_rigid_transform pose{{7, -2, 4}, {2, 3, 0}, {0, 0, 5}, std::numbers::pi / 2};
  REQUIRE(nf_native_transform(owner.get(), source.get(), &pose, &raw) == NF_NATIVE_OK);
  Result placed{raw, nf_native_result_release};
  auto mesh = tessellate(owner.get(), placed.get());
  const auto approximate = inspect(mesh.get());
  CHECK_THAT(approximate.volume, WithinAbs(6000, 1e-7));
  CHECK_THAT(approximate.area, WithinAbs(2200, 1e-7));
  const auto info = query(mesh.get());
  double minimum[3] = {1e100, 1e100, 1e100}, maximum[3] = {-1e100, -1e100, -1e100};
  for (uint64_t i = 0; i < info.vertex_count; ++i)
    for (int axis = 0; axis < 3; ++axis) {
      minimum[axis] = std::min(minimum[axis], info.vertices[i].position[axis]);
      maximum[axis] = std::max(maximum[axis], info.vertices[i].position[axis]);
    }
  const double expected_min[] = {-8, -1, 4}, expected_max[] = {12, 9, 34};
  for (int axis = 0; axis < 3; ++axis) {
    CHECK_THAT(minimum[axis], WithinAbs(expected_min[axis], 1e-7));
    CHECK_THAT(maximum[axis], WithinAbs(expected_max[axis], 1e-7));
  }
}
TEST_CASE("native curved-hole meshes converge and preserve prior buffers and exact source") {
  auto owner = context();
  auto source = solid(owner.get(), true);
  auto coarse = tessellate(owner.get(), source.get(), {0.8, 0.6, 1'000'000});
  const auto coarse_info = query(coarse.get());
  const std::vector<uint32_t> saved(coarse_info.indices,
                                    coarse_info.indices + coarse_info.index_count);
  auto fine = tessellate(owner.get(), source.get(), {0.02, 0.15, 1'000'000});
  const auto c = inspect(coarse.get(), source.get()), f = inspect(fine.get(), source.get());
  CHECK(query(fine.get()).index_count > coarse_info.index_count);
  const double volume = 2250 * std::numbers::pi, area = 1050 * std::numbers::pi;
  CHECK(std::abs(f.volume - volume) < std::abs(c.volume - volume));
  CHECK_THAT(f.volume, WithinAbs(volume, volume * 0.002));
  CHECK_THAT(f.area, WithinAbs(area, area * 0.002));
  CHECK(std::vector<uint32_t>(coarse_info.indices, coarse_info.indices + coarse_info.index_count) ==
        saved);
  nf_native_result_info measures{};
  REQUIRE(nf_native_result_query(source.get(), &measures) == NF_NATIVE_OK);
  CHECK_THAT(measures.volume, WithinAbs(volume, 1e-5));
  CHECK_THAT(measures.area, WithinAbs(area, 1e-5));
  const auto info = query(fine.get());
  size_t bore = 0;
  for (uint64_t i = 0; i < info.vertex_count; ++i) {
    const auto& v = info.vertices[i];
    if (std::abs(std::hypot(v.position[0], v.position[1]) - 5) < 1e-6 &&
        std::abs(v.normal[2]) < 1e-6) {
      CHECK_THAT((v.normal[0] * v.position[0] + v.normal[1] * v.position[1]) / 5,
                 WithinAbs(-1, 1e-6));
      ++bore;
    }
  }
  CHECK(bore > 0);
}
TEST_CASE("native mesh failures publish no partial owner and preserve recovery") {
  auto owner = context();
  auto source = solid(owner.get());
  nf_native_meshing_options options{0.1, 0.2, 1'000'000};
  auto reject = [&](nf_native_context* ctx, const nf_native_meshing_options* value,
                    uint32_t expected) {
    nf_native_mesh* out = reinterpret_cast<nf_native_mesh*>(source.get());
    CHECK(nf_native_tessellate(ctx, source.get(), value, &out) == expected);
    CHECK(out == nullptr);
  };
  auto foreign = context();
  reject(foreign.get(), &options, NF_NATIVE_INVALID);
  reject(nullptr, &options, NF_NATIVE_INVALID);
  reject(owner.get(), nullptr, NF_NATIVE_INVALID);
  CHECK(nf_native_tessellate(owner.get(), source.get(), &options, nullptr) == NF_NATIVE_INVALID);
  nf_native_mesh* out = nullptr;
  CHECK(nf_native_tessellate(owner.get(), nullptr, &options, &out) == NF_NATIVE_INVALID);
  alignas(nf_native_meshing_options) char misaligned[sizeof(options) + 1]{};
  reject(owner.get(), reinterpret_cast<const nf_native_meshing_options*>(misaligned + 1),
         NF_NATIVE_INVALID);
  const auto nan = std::numeric_limits<double>::quiet_NaN(),
             inf = std::numeric_limits<double>::infinity();
  for (auto bad : {0., -1., 1e-8, nan, inf}) {
    options.linear_deflection = bad;
    reject(owner.get(), &options, NF_NATIVE_FAILURE);
  }
  options.linear_deflection = 0.1;
  for (auto bad : {0., -1., 1e-10, 4., nan, inf}) {
    options.angular_deflection = bad;
    reject(owner.get(), &options, NF_NATIVE_FAILURE);
  }
  options.angular_deflection = 0.2;
  for (auto bad : {UINT64_C(0), UINT64_C(11), UINT64_C(1'000'001), UINT64_MAX}) {
    options.max_triangles = bad;
    reject(owner.get(), &options, NF_NATIVE_FAILURE);
  }
  const auto mesh = tessellate(owner.get(), source.get(), {0.1, 0.2, 12});
  CHECK(query(mesh.get()).index_count == 36);
  nf_native_mesh_face face{};
  for (uint64_t bad : {UINT64_C(6), UINT64_MAX}) {
    face.index_count = 42;
    CHECK(nf_native_mesh_face_at(mesh.get(), bad, &face) == NF_NATIVE_INVALID);
    CHECK(face.index_count == 0);
    CHECK(face.face == nullptr);
  }
  CHECK(nf_native_mesh_face_at(mesh.get(), 0, nullptr) == NF_NATIVE_INVALID);
  CHECK(nf_native_mesh_query(mesh.get(), nullptr) == NF_NATIVE_INVALID);
}
#endif
