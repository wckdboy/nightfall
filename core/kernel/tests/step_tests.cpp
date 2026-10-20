// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "nf/kernel/kernel.hpp"
#include "nf/math/constructors.hpp"
#include "snapshot.hpp"

#include <BRepTools.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Reader.hxx>

#include <numbers>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace nf::kernel;
using namespace nf::math;
using Catch::Matchers::WithinAbs;

namespace {
ShapePtr box(IKernel& kernel) {
  const auto paths = std::vector<std::vector<NurbsCurve2>>{
      {make_line(Vec2{}, {3, 0}), make_line(Vec2{3, 0}, {3, 4}), make_line(Vec2{3, 4}, {0, 4}),
       make_line(Vec2{0, 4}, {})}};
  return kernel.extrude({{{2, -1, 7}}, regions(paths, FillRule::kEvenOdd).value(), {0, 0, 5}})
      .value()
      .shape;
}
std::string serialized(const ShapePtr& shape) {
  std::ostringstream stream;
  BRepTools::Write(std::dynamic_pointer_cast<const detail::Snapshot>(shape)->geometry, stream);
  return stream.str();
}
ShapePtr reload(const std::string& text) {
  CHECK(text.starts_with("ISO-10303-21;"));
  CHECK(text.find("AP242") != std::string::npos);
  CHECK(text.find("TESSELLATED") == std::string::npos);
  std::istringstream stream(text);
  STEPControl_Reader reader;
  REQUIRE(reader.ReadStream("round-trip.step", stream) == IFSelect_RetDone);
  reader.SetSystemLengthUnit(1);
  REQUIRE(reader.TransferRoots() > 0);
  auto shape = detail::snapshot(reader.OneShape());
  REQUIRE(shape);
  return *shape;
}
void measures(const ShapePtr& shape, double volume, double area, Vec3 center, std::size_t solids) {
  const auto& m = shape->measures();
  CHECK_THAT(m.volume, WithinAbs(volume, 1e-6));
  CHECK_THAT(m.area, WithinAbs(area, 1e-6));
  CHECK_THAT(m.centroid.x, WithinAbs(center.x, 1e-7));
  CHECK_THAT(m.centroid.y, WithinAbs(center.y, 1e-7));
  CHECK_THAT(m.centroid.z, WithinAbs(center.z, 1e-7));
  CHECK(m.solids == solids);
}
class ForeignShape final : public Shape {
 public:
  Measures measured;
  const Measures& measures() const noexcept override { return measured; }
  std::size_t estimated_bytes() const noexcept override { return 1; }
};
}  // namespace

TEST_CASE("STEP exports exact millimetre solids without changing snapshots or face handles",
          "[step-kernel]") {
  auto kernel = make_occt_kernel().value();
  const auto source = box(*kernel);
  const auto before = serialized(source);
  const auto faces = kernel->faces(source).value();
  const auto text = kernel->export_step({source});
  REQUIRE(text);
  const auto exported = reload(*text);
  measures(exported, 60, 94, {3.5, 1, 9.5}, 1);
  CHECK(exported->measures().faces == 6);
  CHECK(exported->measures().edges == 12);
  CHECK(serialized(source) == before);
  const auto current = kernel->faces(source).value();
  REQUIRE(current.size() == faces.size());
  for (std::size_t i = 0; i < current.size(); ++i)
    CHECK(current[i].entity->same_as(*faces[i].entity));
}

TEST_CASE("STEP preserves all input bodies including coincident shared snapshots",
          "[step-kernel]") {
  auto kernel = make_occt_kernel().value();
  const auto source = box(*kernel);
  auto other = source;
  Vec3 center{3.5, 1, 9.5};
  SECTION("separated bodies") {
    other = kernel->transform(source, {{20, 0, 0}, {}, {0, 0, 1}, 0}).value().shape;
    center.x += 10;
  }
  SECTION("coincident but distinct bodies") {}
  const auto text = kernel->export_step({source, other});
  REQUIRE(text);
  const auto exported = reload(*text);
  measures(exported, 120, 188, center, 2);
  CHECK(exported->measures().faces == 12);
  CHECK(exported->measures().edges == 24);
}

TEST_CASE("STEP retains cavities and analytic circular surfaces", "[step-kernel]") {
  auto kernel = make_occt_kernel().value();
  const auto paths =
      std::vector<std::vector<NurbsCurve2>>{{make_circle({}, 3)}, {make_circle({}, 1)}};
  const auto source =
      kernel->extrude({{}, regions(paths, FillRule::kEvenOdd).value(), {0, 0, 6}}).value().shape;
  const auto before = serialized(source);
  const auto text = kernel->export_step({source});
  REQUIRE(text);
  CHECK(text->find("CYLINDRICAL_SURFACE") != std::string::npos);
  measures(reload(*text), 48 * std::numbers::pi, 64 * std::numbers::pi, {0, 0, 3}, 1);
  CHECK(serialized(source) == before);
}

TEST_CASE("STEP exports current rigid mirrored and multi solid snapshots", "[step-kernel]") {
  auto kernel = make_occt_kernel().value();
  auto source = box(*kernel);
  SECTION("rigid location") {
    source =
        kernel->transform(source, {{10, 5, -3}, {}, {0, 0, 1}, std::numbers::pi / 3}).value().shape;
  }
  SECTION("reflection") {
    source = kernel->mirror(source, {{}, {0, 1, 0}, {1, 0, 0}}).value().shape;
  }
  SECTION("multiple solids in one snapshot") {
    const auto moved = kernel->transform(source, {{20, 0, 0}, {}, {0, 0, 1}, 0}).value().shape;
    source = kernel->boolean_operation(source, {moved}, BooleanOp::kUnion).value().shape;
  }
  const auto before = serialized(source);
  const auto text = kernel->export_step({source});
  REQUIRE(text);
  const auto& m = source->measures();
  measures(reload(*text), m.volume, m.area, m.centroid, m.solids);
  CHECK(serialized(source) == before);
}

TEST_CASE("STEP rejects empty null foreign and invalid snapshot lists without partial output",
          "[step-kernel]") {
  auto kernel = make_occt_kernel().value();
  const auto source = box(*kernel);
  const auto before = serialized(source);
  for (const auto& inputs : {std::vector<ShapePtr>{}, std::vector<ShapePtr>{nullptr},
                             std::vector<ShapePtr>{std::make_shared<ForeignShape>()},
                             std::vector<ShapePtr>{std::make_shared<detail::Snapshot>()},
                             std::vector<ShapePtr>{source, nullptr}}) {
    const auto text = kernel->export_step(inputs);
    REQUIRE_FALSE(text);
    CHECK(text.error().code == "kernel");
    CHECK_FALSE(text.error().message.empty());
    CHECK(serialized(source) == before);
  }
}

TEST_CASE("STEP export accepts retained geometry after its producing facade ends",
          "[step-kernel]") {
  ShapePtr retained;
  {
    auto kernel = make_occt_kernel().value();
    retained = box(*kernel);
  }
  const auto before = serialized(retained);
  auto kernel = make_occt_kernel().value();
  const auto text = kernel->export_step({retained});
  REQUIRE(text);
  measures(reload(*text), 60, 94, {3.5, 1, 9.5}, 1);
  CHECK(serialized(retained) == before);
}
