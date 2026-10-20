// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
// Independent reload of the actual file written by the typed Rust STEP example.
#include <BRepAdaptor_Surface.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Reader.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <array>
#include <cmath>
#include <fstream>
#include <gp_Cylinder.hxx>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include "snapshot.hpp"
namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void near(double actual, double expected, double tolerance, const char* message) {
  require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}
}  // namespace
int main(int argc, char** argv) {
  try {
    require(argc == 2, "usage: step_fixture_probe FILE.step");
    std::ifstream input(argv[1], std::ios::binary);
    require(input.good(), "cannot open STEP fixture");
    const std::string bytes{std::istreambuf_iterator<char>(input), {}};
    require(bytes.starts_with("ISO-10303-21;"), "STEP signature");
    require(bytes.find("AP242") != std::string::npos, "AP242 schema");
    require(bytes.find("TESSELLATED") == std::string::npos, "exact geometry required");
    std::istringstream stream(bytes);
    STEPControl_Reader reader;
    require(reader.ReadStream(argv[1], stream) == IFSelect_RetDone, "STEP parse");
    reader.SetSystemLengthUnit(1);
    require(reader.TransferRoots() > 0, "STEP transfer");
    const auto geometry = reader.OneShape();
    require(BRepCheck_Analyzer(geometry).IsValid(), "full B-rep validity");
    const auto validated = nf::kernel::detail::snapshot(geometry);
    require(validated.has_value(), "closed-solid validation");
    const auto& m = (*validated)->measures();
    require(m.solids == 6 && m.faces == 84 && m.edges == 216, "aggregate topology");
    const double volume = 9600 - 256 * std::numbers::pi;
    const double area = 3520 + 64 * std::numbers::pi;
    near(m.volume, 6 * volume, 6e-5, "aggregate volume mm^3");
    near(m.area, 6 * area, 6e-5, "aggregate area mm^2");
    near(m.centroid.x, 0, 1e-7, "aggregate centroid x mm");
    near(m.centroid.y, 0, 1e-7, "aggregate centroid y mm");
    near(m.centroid.z, 10, 1e-7, "aggregate centroid z mm");
    std::array<bool, 6> seen{};
    for (TopExp_Explorer solids(geometry, TopAbs_SOLID); solids.More(); solids.Next()) {
      const auto solid = nf::kernel::detail::snapshot(solids.Current());
      require(solid.has_value(), "individual closed-solid validation");
      const auto& part = (*solid)->measures();
      require(part.solids == 1 && part.faces == 14 && part.edges == 36, "individual topology");
      // Independent OCCT mass/area integration also checks the conditioned snapshot values.
      GProp_GProps mass, surface;
      BRepGProp::VolumeProperties(solids.Current(), mass);
      BRepGProp::SurfaceProperties(solids.Current(), surface);
      near(mass.Mass(), volume, 1e-5, "OCCT body volume mm^3");
      near(surface.Mass(), area, 1e-5, "OCCT body area mm^2");
      near(part.volume, volume, 1e-5, "body volume mm^3");
      near(part.area, area, 1e-5, "body area mm^2");
      bool found = false;
      for (size_t i = 0; i < seen.size(); ++i) {
        const double angle = static_cast<double>(i) * std::numbers::pi / 3;
        const double x = 60 * std::cos(angle), y = 60 * std::sin(angle);
        const double z = static_cast<double>(i % 2) * 12;
        if (std::hypot(part.centroid.x - x, part.centroid.y - y) > 1e-7) continue;
        require(!seen[i], "duplicate world-space body");
        seen[i] = found = true;
        near(part.centroid.z, z + 4, 1e-7, "world centroid z mm");
        const double dx = 20 * std::abs(std::cos(angle)) + 15 * std::abs(std::sin(angle));
        const double dy = 20 * std::abs(std::sin(angle)) + 15 * std::abs(std::cos(angle));
        near(part.minimum.x, x - dx, 1e-6, "world minimum x mm");
        near(part.maximum.x, x + dx, 1e-6, "world maximum x mm");
        near(part.minimum.y, y - dy, 1e-6, "world minimum y mm");
        near(part.maximum.y, y + dy, 1e-6, "world maximum y mm");
        near(part.minimum.z, z, 1e-6, "world minimum z mm");
        near(part.maximum.z, z + 8, 1e-6, "world maximum z mm");
      }
      require(found, "unexpected world centroid");
      size_t cylinders = 0;
      for (TopExp_Explorer faces(solids.Current(), TopAbs_FACE); faces.More(); faces.Next()) {
        BRepAdaptor_Surface face(TopoDS::Face(faces.Current()));
        if (face.GetType() != GeomAbs_Cylinder) continue;
        ++cylinders;
        near(face.Cylinder().Radius(), 4, 1e-8, "exact hole radius mm");
      }
      require(cylinders == 8, "two exact four-quadrant cylindrical holes per body");
    }
    for (bool present : seen) require(present, "missing world-space body");
    std::cout << std::fixed << std::setprecision(9) << "Rust STEP reload: solids=" << m.solids
              << " faces=" << m.faces << " edges=" << m.edges << " volume=" << m.volume
              << " mm^3 area=" << m.area
              << " mm^2; 12 exact radius-4 mm holes; bounds/centroids passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
