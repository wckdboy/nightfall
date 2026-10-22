// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "evaluator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numbers>
#include <set>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "nf/ir/migration.hpp"

using namespace nf;
using namespace nf::kernel_service;
using Catch::Matchers::WithinAbs;

namespace {
constexpr double pi = std::numbers::pi;
const std::filesystem::path directory{NF_REFERENCE_PARTS_DIR};
const std::set<std::string> catalogue{
    "mounting_plate", "washer",          "hex_spacer",    "angle_bracket",
    "channel",        "enclosure",       "stepped_shaft", "flanged_bushing",
    "conical_nozzle", "spherical_knob",  "toroidal_seal", "semicircular_saddle",
    "slotted_link",   "elliptic_duct",   "spline_wedge",  "gusset",
    "offset_foot",    "twin_boss_plate", "drilled_block", "intersected_rail"};

// Analytic geometry, independent of OCCT and the model's feature construction. Areas are
// optional for the elliptic duct, whose perimeter has no elementary closed form.
struct Expected {
  double volume;
  math::Vec3 centroid, minimum, maximum;
  std::optional<double> area;
  double area_tolerance = 1e-5;
};
void vector_near(math::Vec3 actual, math::Vec3 expected, double tolerance) {
  CHECK_THAT(actual.x, WithinAbs(expected.x, tolerance));
  CHECK_THAT(actual.y, WithinAbs(expected.y, tolerance));
  CHECK_THAT(actual.z, WithinAbs(expected.z, tolerance));
}
void verify(const Evaluation& result, const Expected& expected) {
  REQUIRE(result.ok());
  REQUIRE(result.bodies.contains("part"));
  CHECK(std::ranges::count_if(result.bodies, [](const auto& item) {
          return static_cast<bool>(item.second.shape);
        }) == 1);
  const auto& body = result.bodies.at("part");
  REQUIRE(body.shape);
  REQUIRE(body.hash);
  const auto& m = body.shape->measures();
  CHECK(m.solids == 1);
  CHECK(m.faces > 0);
  CHECK(m.edges > 0);
  CHECK_THAT(m.volume, WithinAbs(expected.volume, 1e-5));
  CHECK(std::isfinite(m.area));
  CHECK(m.area > 0);
  if (expected.area) CHECK_THAT(m.area, WithinAbs(*expected.area, expected.area_tolerance));
  vector_near(m.centroid, expected.centroid, 1e-6);
  vector_near(m.minimum, expected.minimum, 1e-5);
  vector_near(m.maximum, expected.maximum, 1e-5);
  REQUIRE_FALSE(body.names.empty());
  for (const auto& name : body.names) {
    REQUIRE(name.entity);
    CHECK(name.entity->owner() == body.shape);
    CHECK(
        std::ranges::any_of(result.features, [&](const auto& f) { return f.id == name.feature; }));
  }
}
template <class T>
void diagnostics(const T& result) {
  if (!result)
    for (const auto& error : result.error())
      WARN(error.pointer << ": " << error.code << ": " << error.message);
  REQUIRE(result);
}
Evaluation build(Evaluator& evaluator, const std::string& text) {
  auto result = evaluator.evaluate(text);
  diagnostics(result);
  for (const auto& feature : result->features)
    for (const auto& error : feature.errors)
      WARN(error.pointer << ": " << error.code << ": " << error.message);
  REQUIRE(result->ok());
  return std::move(*result);
}
std::string write(const ir::Document& document) {
  auto result = ir::write_document(document);
  diagnostics(result);
  return *result;
}
void exercise(const char* name, const char* parameter, std::array<double, 3> values,
              Expected (*geometry)(double)) {
  CAPTURE(name);
  REQUIRE(catalogue.contains(name));
  std::ifstream stream(directory / (std::string(name) + ".nightfall"));
  REQUIRE(stream.is_open());
  const std::string text{std::istreambuf_iterator<char>{stream}, {}};
  auto document = ir::read_document(text);
  diagnostics(document);
  // The stored parts are version 1; written output is the migrated, current version.
  CHECK(ir::json::parse(write(*document)).value() == ir::migrate(ir::json::parse(text).value()));
  auto input = std::ranges::find(document->params, parameter, &ir::Parameter::name);
  REQUIRE(input != document->params.end());
  auto backend = kernel::make_occt_kernel().value();
  Evaluator evaluator(*backend, {128, 64 * 1024 * 1024});
  const auto original = build(evaluator, text);
  verify(original, geometry(values[0]));
  CHECK(original.kernel_calls > 0);
  CHECK(original.cache_hits == 0);
  const auto warm = build(evaluator, write(*document));
  verify(warm, geometry(values[0]));
  CHECK(warm.kernel_calls == 0);
  CHECK(warm.cache_hits == document->features.size());
  CHECK(warm.bodies.at("part").shape == original.bodies.at("part").shape);

  for (std::size_t i = 1; i < values.size(); ++i) {
    CAPTURE(values[i]);
    input->value = std::to_string(values[i]) + " mm";
    const auto edited_text = write(*document);
    const auto edited = build(evaluator, edited_text);
    verify(edited, geometry(values[i]));
    CHECK(edited.kernel_calls > 0);
    CHECK(edited.bodies.at("part").hash != original.bodies.at("part").hash);
    Evaluator cold(*backend, {128, 64 * 1024 * 1024});
    const auto rebuilt = build(cold, edited_text);
    verify(rebuilt, geometry(values[i]));
    CHECK(rebuilt.cache_hits == 0);
    CHECK(edited.kernel_calls <= rebuilt.kernel_calls);
    CHECK(edited.bodies.at("part").hash == rebuilt.bodies.at("part").hash);

    input->value = std::to_string(values[i] / 10) + " cm";
    const auto equivalent = build(evaluator, write(*document));
    verify(equivalent, geometry(values[i]));
    CHECK(equivalent.kernel_calls == 0);
    CHECK(equivalent.cache_hits == document->features.size());
    CHECK(equivalent.bodies.at("part").shape == edited.bodies.at("part").shape);
  }
  const auto restored = build(evaluator, text);
  verify(restored, geometry(values[0]));
  CHECK(restored.kernel_calls == 0);
  CHECK(restored.cache_hits == document->features.size());
  CHECK(restored.bodies.at("part").shape == original.bodies.at("part").shape);
  evaluator.set_cache_limits({0, 0});
  CHECK(evaluator.cache_usage().entries == 0);
  const auto evicted = build(evaluator, text);
  verify(evicted, geometry(values[0]));
  CHECK(evicted.kernel_calls == original.kernel_calls);
  CHECK(evicted.cache_hits == 0);
  CHECK(evaluator.cache_usage().entries == 0);
  verify(original, geometry(values[0]));  // retained geometry/names outlive edits and eviction
}
}  // namespace

TEST_CASE("reference catalogue contains exactly the twenty exercised documents", "[reference]") {
  std::set<std::string> files;
  for (const auto& entry : std::filesystem::directory_iterator(directory))
    if (entry.path().extension() == ".nightfall") files.insert(entry.path().stem().string());
  CHECK(files == catalogue);
}

TEST_CASE("reference part: mounting plate", "[reference]") {
  exercise("mounting_plate", "thickness", {4, 2, 7.5}, [](double t) -> Expected {
    const double a = 2400 - 36 * pi;
    return {a * t, {0, 0, t / 2}, {-30, -20, 0}, {30, 20, t}, 2 * a + (200 + 24 * pi) * t};
  });
}
TEST_CASE("reference part: washer", "[reference]") {
  exercise("washer", "thickness", {2, 1, 4}, [](double t) -> Expected {
    return {119 * pi * t, {0, 0, t / 2}, {-12, -12, 0}, {12, 12, t}, (238 + 34 * t) * pi};
  });
}
TEST_CASE("reference part: hex spacer", "[reference]") {
  exercise("hex_spacer", "height", {12, 6, 18}, [](double h) -> Expected {
    const double a = 150 * std::sqrt(3.0) - 9 * pi, y = 5 * std::sqrt(3.0);
    return {a * h, {0, 0, h / 2}, {-10, -y, 0}, {10, y, h}, 2 * a + (60 + 6 * pi) * h};
  });
}
TEST_CASE("reference part: angle bracket", "[reference]") {
  exercise("angle_bracket", "width", {30, 20, 45}, [](double w) -> Expected {
    return {264 * w, {w / 2, 3408.0 / 264, 2088.0 / 264}, {0, 0, 0}, {w, 40, 30}, 528 + 140 * w};
  });
}
TEST_CASE("reference part: channel", "[reference]") {
  exercise("channel", "length", {50, 25, 75}, [](double l) -> Expected {
    return {192 * l, {0, 1308.0 / 192, l / 2}, {-15, 0, 0}, {15, 20, l}, 384 + 134 * l};
  });
}
TEST_CASE("reference part: enclosure", "[reference]") {
  exercise("enclosure", "height", {20, 12, 28}, [](double h) -> Expected {
    const double v = 564 * h + 5508;
    return {v,
            {0, 0, (2400 * h * h - 1836 * (h * h - 9)) / (2 * v)},
            {-30, -20, 0},
            {30, 20, h},
            4800 + 200 * h + 176 * (h - 3)};
  });
}
TEST_CASE("reference part: stepped shaft", "[reference]") {
  exercise("stepped_shaft", "length", {30, 20, 45}, [](double l) -> Expected {
    return {38 * pi * l, {0, 0, 22 * l / 57}, {-8, -8, 0}, {8, 8, l}, (128 + 12 * l) * pi};
  });
}
TEST_CASE("reference part: flanged bushing", "[reference]") {
  exercise("flanged_bushing", "height", {15, 10, 22}, [](double h) -> Expected {
    const double a = 27 * h + 324;
    return {a * pi,
            {0, 0, (27 * h * h / 2 + 486) / a},
            {-12, -12, 0},
            {12, 12, h},
            (306 + 18 * h) * pi};
  });
}
TEST_CASE("reference part: conical nozzle", "[reference]") {
  exercise("conical_nozzle", "height", {20, 12, 30}, [](double h) -> Expected {
    return {48 * pi * h,
            {0, 0, 17 * h / 48},
            {-10, -10, 0},
            {10, 10, h},
            pi * (108 + 14 * std::sqrt(h * h + 36) + 4 * h)};
  });
}
TEST_CASE("reference part: spherical knob", "[reference]") {
  exercise("spherical_knob", "radius", {10, 7, 12}, [](double r) -> Expected {
    return {4 * pi * r * r * r / 3, {0, 0, r}, {-r, -r, 0}, {r, r, 2 * r}, 4 * pi * r * r};
  });
}
TEST_CASE("reference part: toroidal seal", "[reference]") {
  exercise("toroidal_seal", "radius", {3, 2, 4}, [](double r) -> Expected {
    return {40 * pi * pi * r * r,
            {0, 0, 0},
            {-20 - r, -20 - r, -r},
            {20 + r, 20 + r, r},
            80 * pi * pi * r};
  });
}
TEST_CASE("reference part: semicircular saddle", "[reference]") {
  exercise("semicircular_saddle", "width", {8, 4, 12}, [](double w) -> Expected {
    return {32 * pi * w,
            {0, 49 / (3 * pi), w / 2},
            {-10, 0, 0},
            {10, 10, w},
            64 * pi + (16 * pi + 8) * w};
  });
}
TEST_CASE("reference part: slotted link", "[reference]") {
  exercise("slotted_link", "thickness", {6, 3, 9}, [](double t) -> Expected {
    const double a = 280 + 32 * pi;
    return {a * t, {0, 0, t / 2}, {-21, -6, 0}, {21, 6, t}, 2 * a + (100 + 16 * pi) * t};
  });
}
TEST_CASE("reference part: elliptic duct", "[reference]") {
  exercise("elliptic_duct", "length", {20, 10, 30}, [](double l) -> Expected {
    const double x = std::sqrt(84.0), y = std::sqrt(52.0);
    return {28 * pi * l, {0, 0, l / 2}, {-x, -y, 0}, {x, y, l}, {}};
  });
}
TEST_CASE("reference part: spline wedge", "[reference]") {
  exercise("spline_wedge", "thickness", {5, 2, 8}, [](double t) -> Expected {
    // OCCT's numerical surface integral differs by up to 1.73e-4 mm² here; retain an
    // explicit area budget without relaxing volume, centroid or bounding-box checks.
    return {200 * t / 3,
            {10, 2, t / 2},
            {0, 0, 0},
            {20, 5, t},
            400.0 / 3 + (20 + 10 * (std::sqrt(2.0) + std::asinh(1.0))) * t,
            2e-4};
  });
}
TEST_CASE("reference part: gusset", "[reference]") {
  exercise("gusset", "thickness", {4, 2, 6}, [](double t) -> Expected {
    return {600 * t, {t / 2, 10, 40.0 / 3}, {0, 0, 0}, {t, 30, 40}, 1200 + 120 * t};
  });
}
TEST_CASE("reference part: offset foot", "[reference]") {
  exercise("offset_foot", "thickness", {6, 3, 9}, [](double t) -> Expected {
    return {200 * t, {25, 15, 7 + t / 2}, {20, 5, 7}, {30, 25, 7 + t}, 400 + 60 * t};
  });
}
TEST_CASE("reference part: twin boss plate", "[reference]") {
  exercise("twin_boss_plate", "height", {8, 5, 12}, [](double h) -> Expected {
    const double v = 3200 + 32 * pi * h;
    return {v,
            {0, 0, (6400 + 32 * pi * h * (4 + h / 2)) / v},
            {-20, -10, 0},
            {20, 10, 4 + h},
            2080 + 16 * pi * h};
  });
}
TEST_CASE("reference part: drilled block", "[reference]") {
  exercise("drilled_block", "height", {12, 8, 16}, [](double h) -> Expected {
    const double a = 600 - 8 * pi;
    return {a * h, {0, 0, h / 2}, {-15, -10, 0}, {15, 10, h}, 2 * a + (100 + 8 * pi) * h};
  });
}
TEST_CASE("reference part: intersected rail", "[reference]") {
  exercise("intersected_rail", "height", {10, 6, 14}, [](double h) -> Expected {
    return {400 * h, {20, 10, h / 2}, {10, 0, 0}, {30, 20, h}, 800 + 80 * h};
  });
}
