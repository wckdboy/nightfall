// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "kernel_bridge.h"
#include "nf/math/vec.hpp"
#include "part_fixture.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf;
using Catch::Matchers::WithinAbs;
namespace fixture = nf::kernel_service::test;
namespace {
using Session = std::unique_ptr<nf_session, decltype(&nf_session_destroy)>;
using Result = std::unique_ptr<nf_result, decltype(&nf_result_free)>;
using ir::json::Value;
Session session() {
  Session value{nf_session_create(), nf_session_destroy};
  REQUIRE(value);
  CHECK(nf_session_active_feature(value.get()) == NF_NO_ACTIVE_FEATURE);
  return value;
}
Result build(nf_session* s, const std::string& text, const nf_mesh_options* options = nullptr) {
  Result value{nf_session_build(s, text.data(), text.size(), options), nf_result_free};
  REQUIRE(value);
  CHECK(nf_session_active_feature(s) == NF_NO_ACTIVE_FEATURE);
  return value;
}
Value manifest(const Result& result) {
  std::size_t bytes = 0;
  const auto* text = nf_result_json(result.get(), &bytes);
  REQUIRE(text);
  CHECK(bytes > 0);
  CHECK(bytes <= ir::kMaxDocumentBytes);
  CHECK(text[bytes] == '\0');
  const auto parsed = ir::json::parse({text, bytes});
  REQUIRE(parsed);
  return *parsed;
}
const ir::json::Array& array(const Value& value, const char* key) {
  REQUIRE(value.find(key));
  REQUIRE(value.find(key)->if_array());
  return *value.find(key)->if_array();
}
double number(const Value& value, const char* key) {
  REQUIRE(value.find(key));
  REQUIRE(value.find(key)->if_number());
  return *value.find(key)->if_number();
}
void vector_near(const Value& value, const char* key, math::Vec3 expected, double tolerance) {
  const auto& values = array(value, key);
  REQUIRE(values.size() == 3);
  CHECK_THAT(*values[0].if_number(), WithinAbs(expected.x, tolerance));
  CHECK_THAT(*values[1].if_number(), WithinAbs(expected.y, tolerance));
  CHECK_THAT(*values[2].if_number(), WithinAbs(expected.z, tolerance));
}
void failed(const Result& result, std::string_view code = {}) {
  const auto data = manifest(result);
  const auto& errors = array(data, "errors");
  REQUIRE_FALSE(errors.empty());
  if (!code.empty())
    CHECK(std::ranges::any_of(
        errors, [&](const auto& error) { return *error.find("code")->if_string() == code; }));
  CHECK(array(data, "bodies").empty());
  CHECK(array(data, "face_names").empty());
  std::size_t count = 123;
  CHECK(nf_result_vertices(result.get(), &count) == nullptr);
  CHECK(count == 0);
  CHECK(nf_result_indices(result.get(), &count) == nullptr);
  CHECK(count == 0);
  CHECK(nf_result_faces(result.get(), &count) == nullptr);
  CHECK(count == 0);
}
math::Vec3 vector(const double* p) { return {p[0], p[1], p[2]}; }
void mesh(const Result& result, double exact_volume, double exact_area, double relative = 0.01) {
  const auto data = manifest(result);
  CHECK(array(data, "errors").empty());
  const auto& bodies = array(data, "bodies");
  std::size_t nv = 0, ni = 0, nf = 0;
  const auto* vertices = nf_result_vertices(result.get(), &nv);
  const auto* indices = nf_result_indices(result.get(), &ni);
  const auto* faces = nf_result_faces(result.get(), &nf);
  REQUIRE(vertices);
  REQUIRE(indices);
  REQUIRE(faces);
  CHECK(nv <= 3'000'000);
  CHECK(ni <= 3'000'000);
  CHECK(ni % 3 == 0);
  CHECK(array(data, "face_names").size() == nf);
  std::size_t count = 0;
  for (std::size_t i = 0; i < nf; ++i) {
    CHECK(faces[i].body_index < bodies.size());
    CHECK(faces[i].first_index == count);
    CHECK(faces[i].index_count > 0);
    CHECK(faces[i].index_count % 3 == 0);
    count += faces[i].index_count;
  }
  REQUIRE(count == ni);
  bool finite = true, unit = true, outward = true;
  for (std::size_t i = 0; i < nv; ++i) {
    for (std::size_t j = 0; j < 3; ++j)
      finite =
          finite && std::isfinite(vertices[i].position[j]) && std::isfinite(vertices[i].normal[j]);
    unit = unit && std::abs(math::length(vector(vertices[i].normal)) - 1) <= 1e-6;
  }
  double volume = 0, area = 0;
  const auto origin = vector(vertices[0].position);
  for (std::size_t i = 0; i < ni; i += 3) {
    for (std::size_t j = 0; j < 3; ++j) REQUIRE(indices[i + j] < nv);
    const auto a = vector(vertices[indices[i]].position);
    const auto b = vector(vertices[indices[i + 1]].position);
    const auto c = vector(vertices[indices[i + 2]].position);
    const auto cross = math::cross(b - a, c - a);
    outward = outward && math::dot(cross, vector(vertices[indices[i]].normal) +
                                              vector(vertices[indices[i + 1]].normal) +
                                              vector(vertices[indices[i + 2]].normal)) > 0;
    area += math::length(cross) / 2;
    volume += math::dot(a - origin, math::cross(b - origin, c - origin)) / 6;
  }
  CHECK(finite);
  CHECK(unit);
  CHECK(outward);
  CHECK_THAT(volume, WithinAbs(exact_volume, exact_volume * relative));
  CHECK_THAT(area, WithinAbs(exact_area, exact_area * relative));
}
}  // namespace

TEST_CASE("C bridge builds exact parts with indexed meshes and current face provenance",
          "[bridge]") {
  auto s = session();
  const auto result = build(s.get(), fixture::source(fixture::part()));
  const auto data = manifest(result);
  const auto& bodies = array(data, "bodies");
  REQUIRE(bodies.size() == 1);
  CHECK(*bodies[0].find("id")->if_string() == "plate");
  CHECK(bodies[0].find("hash")->if_string()->size() == 64);
  CHECK_THAT(number(bodies[0], "volume"), WithinAbs(6000, 1e-7));
  CHECK_THAT(number(bodies[0], "area"), WithinAbs(2200, 1e-7));
  vector_near(bodies[0], "centroid", {8, 10, 15}, 1e-7);
  vector_near(bodies[0], "minimum", {3, 0, 0}, 1e-6);
  vector_near(bodies[0], "maximum", {13, 20, 30}, 1e-6);
  CHECK(number(bodies[0], "solids") == 1);
  CHECK(number(bodies[0], "faces") == 6);
  const auto& features = array(data, "features");
  REQUIRE(features.size() == 3);
  for (const auto& feature : features) CHECK(*feature.find("status")->if_string() == "built");
  std::size_t named_sides = 0;
  for (const auto& names : array(data, "face_names")) {
    REQUIRE(names.if_array());
    REQUIRE_FALSE(names.if_array()->empty());
    for (const auto& name : *names.if_array()) {
      CHECK(*name.find("feature")->if_string() == "base");
      if (const auto* from = name.find("from")) {
        CHECK(*from->if_string() == "outline/r");
        ++named_sides;
      }
    }
  }
  CHECK(named_sides == 4);
  mesh(result, 6000, 2200, 1e-9);
}

TEST_CASE("C bridge owns retained results independently of sessions and later edits", "[bridge]") {
  auto s = session();
  auto d = fixture::part();
  const auto original = fixture::source(d);
  const auto result = build(s.get(), original);
  std::size_t bytes = 0;
  const auto* retained_text = nf_result_json(result.get(), &bytes);
  const std::string copy{retained_text, bytes};
  const auto warm = manifest(build(s.get(), original));
  CHECK(number(*warm.find("cache"), "hits") == 3);
  CHECK(number(*warm.find("cache"), "kernel_calls") == 0);
  d.params[0].value = "15 mm";
  mesh(build(s.get(), fixture::source(d)), 3000, 1300, 1e-9);
  failed(build(s.get(), "{}"));
  const auto recovered = manifest(build(s.get(), original));
  CHECK(number(*recovered.find("cache"), "hits") == 3);
  s.reset();
  CHECK(std::string(retained_text, bytes) == copy);
  CHECK(nf_result_json(result.get(), nullptr) == retained_text);
  mesh(result, 6000, 2200, 1e-9);
}

TEST_CASE("C bridge rejects malformed and oversized requests without accessing excess bytes",
          "[bridge]") {
  auto s = session();
  for (const auto& text : std::initializer_list<std::string>{"", "{}", "{", std::string(1, '\xff'),
                                                             std::string("{}\0", 3)})
    failed(build(s.get(), text));
  const auto text = fixture::source(fixture::part());
  failed(build(nullptr, text), "kernel");
  Result null_input{nf_session_build(s.get(), nullptr, 1, nullptr), nf_result_free};
  failed(null_input, "parse");
  Result empty{nf_session_build(s.get(), nullptr, 0, nullptr), nf_result_free};
  failed(empty, "parse");
  // Deliberately tiny backing storage: the size check must precede any input access.
  Result oversized{nf_session_build(s.get(), "x", ir::kMaxDocumentBytes + 1, nullptr),
                   nf_result_free};
  failed(oversized, "bounds");
  Result maximal{
      nf_session_build(s.get(), nullptr, std::numeric_limits<std::size_t>::max(), nullptr),
      nf_result_free};
  failed(maximal, "bounds");
  failed(build(s.get(), fixture::source(ir::Document{})), "kernel");
  mesh(build(s.get(), text), 6000, 2200, 1e-9);
}

TEST_CASE("C bridge never returns partial-success meshes on independent feature failures",
          "[bridge]") {
  auto s = session();
  auto d = fixture::part(2);
  d.params[0].value = "0 mm";
  d.features.push_back(fixture::feature(
      "independent", "extrude",
      R"({"profile":{"sketch":"outline"},"distance":10,"result":{"new":"good","body_type":"brep"}})"));
  const auto result = build(s.get(), fixture::source(d));
  failed(result, "kernel");
  const auto data = manifest(result);
  const auto& errors = array(data, "errors");
  REQUIRE(errors.size() == 1);
  CHECK(number(errors[0], "feature_index") == 1);
  CHECK(errors[0].find("pointer")->if_string()->starts_with("/features/1"));
  const auto& features = array(data, "features");
  REQUIRE(features.size() == 3);
  CHECK(*features[1].find("status")->if_string() == "failed");
  CHECK(*features[2].find("status")->if_string() == "built");
  d.params[0].value = "1 deg";
  failed(build(s.get(), fixture::source(d)), "unit");
  d.params[0].value = "30 mm";
  mesh(build(s.get(), fixture::source(d)), 8000, 3200, 1e-9);
}

TEST_CASE("C bridge caps diagnostic expansion and accepts the exact input byte boundary",
          "[bridge]") {
  auto s = session();
  auto text = fixture::source(fixture::part(2));
  text.resize(ir::kMaxDocumentBytes, ' ');
  mesh(build(s.get(), text), 6000, 2200, 1e-9);
  text = fixture::source(fixture::part(2));
  const auto end = text.rfind('}');
  REQUIRE(end != std::string::npos);
  // RFC 6901 doubles each tilde in the unknown-field pointer, exceeding the result limit
  // even though the source is within its byte budget. Return a small structured error.
  text.insert(end, ",\"" + std::string(ir::kMaxDocumentBytes / 2, '~') + "\":null");
  REQUIRE(text.size() < ir::kMaxDocumentBytes);
  failed(build(s.get(), text), "bounds");
}

TEST_CASE("C bridge handles suppression rollback sorted bodies and consumed tools", "[bridge]") {
  auto s = session();
  auto d = fixture::part();
  d.features.back().suppressed = true;
  auto data = manifest(build(s.get(), fixture::source(d)));
  CHECK(*array(data, "features")[2].find("status")->if_string() == "suppressed");
  vector_near(array(data, "bodies")[0], "centroid", {5, 10, 15}, 1e-7);
  d.features.back().suppressed.reset();
  d.history_end = "base";
  data = manifest(build(s.get(), fixture::source(d)));
  CHECK(*array(data, "features")[2].find("status")->if_string() == "rolled_back");
  vector_near(array(data, "bodies")[0], "centroid", {5, 10, 15}, 1e-7);
  d = fixture::part(2);
  d.features.push_back(fixture::feature(
      "tool", "extrude",
      R"({"profile":{"sketch":"outline"},"distance":10,"result":{"new":"aaa","body_type":"brep"}})"));
  auto result = build(s.get(), fixture::source(d));
  data = manifest(result);
  REQUIRE(array(data, "bodies").size() == 2);
  CHECK(*array(data, "bodies")[0].find("id")->if_string() == "aaa");
  CHECK(*array(data, "bodies")[1].find("id")->if_string() == "plate");
  std::size_t count = 0;
  const auto* faces = nf_result_faces(result.get(), &count);
  REQUIRE(count == 12);
  CHECK(faces[0].body_index == 0);
  CHECK(faces[6].body_index == 1);
  mesh(result, 8000, 3200, 1e-9);
  d.features.push_back(
      fixture::feature("combine", "boolean", R"({"op":"union","target":"plate","tools":["aaa"]})"));
  result = build(s.get(), fixture::source(d));
  CHECK(array(manifest(result), "bodies").size() == 1);
  mesh(result, 6000, 2200, 1e-9);
}

TEST_CASE("C bridge enforces a total mesh budget and recovers after meshing errors", "[bridge]") {
  auto s = session();
  auto d = fixture::part(2);
  const auto text = fixture::source(d);
  for (const auto options : {nf_mesh_options{0.1, 0.2, 0}, nf_mesh_options{0.1, 0.2, 1'000'001},
                             nf_mesh_options{0, 0.2, 100}, nf_mesh_options{0.1, 4, 100},
                             nf_mesh_options{std::numeric_limits<double>::infinity(), 0.2, 100},
                             nf_mesh_options{0.1, std::numeric_limits<double>::quiet_NaN(), 100}})
    failed(build(s.get(), text, &options));
  nf_mesh_options options{0.1, 0.2, 11};
  failed(build(s.get(), text, &options));
  options.max_triangles = 12;
  mesh(build(s.get(), text, &options), 6000, 2200, 1e-9);
  d.features.push_back(fixture::feature(
      "second", "extrude",
      R"({"profile":{"sketch":"outline"},"distance":10,"result":{"new":"aaa","body_type":"brep"}})"));
  const auto two = fixture::source(d);
  failed(build(s.get(), two, &options));  // each body fits; their sum does not
  options.max_triangles = 23;
  failed(build(s.get(), two, &options));
  options.max_triangles = 24;
  mesh(build(s.get(), two, &options), 8000, 3200, 1e-9);
  mesh(build(s.get(), text), 6000, 2200, 1e-9);
}

TEST_CASE("C bridge nullable accessors and destruction are safe", "[bridge]") {
  std::size_t count = 123;
  CHECK(nf_result_json(nullptr, &count) == nullptr);
  CHECK(count == 0);
  CHECK(nf_result_vertices(nullptr, &count) == nullptr);
  CHECK(count == 0);
  CHECK(nf_result_indices(nullptr, &count) == nullptr);
  CHECK(count == 0);
  CHECK(nf_result_faces(nullptr, &count) == nullptr);
  CHECK(count == 0);
  CHECK(nf_result_json(nullptr, nullptr) == nullptr);
  CHECK(nf_result_vertices(nullptr, nullptr) == nullptr);
  CHECK(nf_result_indices(nullptr, nullptr) == nullptr);
  CHECK(nf_result_faces(nullptr, nullptr) == nullptr);
  CHECK(nf_session_active_feature(nullptr) == NF_NO_ACTIVE_FEATURE);
  nf_result_free(nullptr);
  nf_session_destroy(nullptr);
}

TEST_CASE("C bridge sessions isolate caches and rebind face names after producer edits",
          "[bridge]") {
  auto a = session(), b = session();
  auto d = fixture::part(2);
  const auto text = fixture::source(d);
  const auto first = build(a.get(), text);
  const auto cold = manifest(build(b.get(), text));
  CHECK(number(*cold.find("cache"), "hits") == 0);
  CHECK(number(*cold.find("cache"), "kernel_calls") == 1);
  d.features[1].id = "renamed";
  const auto renamed = build(a.get(), fixture::source(d));
  const auto data = manifest(renamed);
  CHECK(number(*data.find("cache"), "hits") == 2);
  for (const auto& names : array(data, "face_names")) {
    REQUIRE_FALSE(names.if_array()->empty());
    for (const auto& name : *names.if_array())
      CHECK(*name.find("feature")->if_string() == "renamed");
  }
  const auto original = manifest(first);
  for (const auto& names : array(original, "face_names"))
    for (const auto& name : *names.if_array()) CHECK(*name.find("feature")->if_string() == "base");
}

TEST_CASE("all twenty reference documents cross the C ABI with correct indexed geometry",
          "[bridge]") {
  auto s = session();
  std::size_t count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(NF_REFERENCE_PARTS_DIR)) {
    if (entry.path().extension() != ".nightfall") continue;
    CAPTURE(entry.path().filename().string());
    std::ifstream stream(entry.path());
    const std::string text{std::istreambuf_iterator<char>{stream}, {}};
    const auto result = build(s.get(), text);
    const auto data = manifest(result);
    const auto& bodies = array(data, "bodies");
    REQUIRE(bodies.size() == 1);
    CHECK(*bodies[0].find("id")->if_string() == "part");
    mesh(result, number(bodies[0], "volume"), number(bodies[0], "area"));
    ++count;
  }
  CHECK(count == 20);
}
