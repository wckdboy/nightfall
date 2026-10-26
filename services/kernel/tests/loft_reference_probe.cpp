// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
// Private OCCT data export for independent Rust integration, never a modeling API.
#include "evaluator.hpp"
#include "nf/kernel/occt_geom.hpp"
#include "snapshot.hpp"

#include <BRepAdaptor_Curve2d.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepTools.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <sstream>
#include <stdexcept>

using namespace nf;
using ir::json::Array;
using ir::json::Object;
namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
std::string read(const char* path) {
  std::ifstream file(path, std::ios::binary);
  require(file.good(), "cannot read fixture");
  return {std::istreambuf_iterator<char>{file}, {}};
}
Array vec(const gp_Vec& v) { return {v.X(), v.Y(), v.Z()}; }
Array point(const gp_Pnt& p) { return {p.X(), p.Y(), p.Z()}; }
Array values(std::span<const double> values) {
  Array out;
  for (double v : values) out.push_back(v);
  return out;
}
std::string geometry_bytes(const kernel::ShapePtr& input) {
  std::ostringstream stream;
  BRepTools::Write(std::dynamic_pointer_cast<const kernel::detail::Snapshot>(input)->geometry,
                   stream);
  return stream.str();
}
void rectangle(const TopoDS_Face& face, const BRepAdaptor_Surface& surface) {
  const double u0 = surface.FirstUParameter(), u1 = surface.LastUParameter();
  const double v0 = surface.FirstVParameter(), v1 = surface.LastVParameter();
  require(std::isfinite(u0) && std::isfinite(u1) && std::isfinite(v0) && std::isfinite(v1) &&
              u0 < u1 && v0 < v1,
          "invalid patch domain");
  std::array<bool, 4> seen{};
  std::size_t wires = 0;
  for (TopExp_Explorer found(face, TopAbs_WIRE); found.More(); found.Next()) ++wires;
  require(wires == 1, "patch holes or missing wire");
  const auto near = [](double x, double y) {
    return std::abs(x - y) <= 1e-10 * std::max({1., std::abs(x), std::abs(y)});
  };
  for (TopExp_Explorer found(face, TopAbs_EDGE); found.More(); found.Next()) {
    const BRepAdaptor_Curve2d curve(TopoDS::Edge(found.Current()), face);
    require(curve.GetType() == GeomAbs_Line, "nonlinear patch trim");
    auto a = curve.Value(curve.FirstParameter()), b = curve.Value(curve.LastParameter());
    int side = -1;
    for (int i = 0; i < 4; ++i) {
      const bool fixed_u = i < 2;
      const double fixed = i == 0 ? u0 : i == 1 ? u1 : i == 2 ? v0 : v1;
      const double lo = fixed_u ? v0 : u0, hi = fixed_u ? v1 : u1;
      const double af = fixed_u ? a.X() : a.Y(), bf = fixed_u ? b.X() : b.Y();
      const double av = fixed_u ? a.Y() : a.X(), bv = fixed_u ? b.Y() : b.X();
      if (near(af, fixed) && near(bf, fixed) && near(std::min(av, bv), lo) &&
          near(std::max(av, bv), hi))
        side = i;
    }
    require(side >= 0 && !seen[static_cast<std::size_t>(side)],
            "incomplete or repeated rectangle side");
    seen[static_cast<std::size_t>(side)] = true;
  }
  require(std::ranges::all_of(seen, [](bool x) { return x; }), "incomplete rectangle");
}
Object export_case(const kernel_service::Evaluation& result, double waist, double ms) {
  require(result.ok(), "loft evaluation failed");
  require(result.features.size() == 4, "minimal feature coverage");
  const auto& body = result.bodies.at("cell_vanes");
  require(body.hash.has_value(), "missing body hash");
  const auto shape = std::dynamic_pointer_cast<const kernel::detail::Snapshot>(body.shape);
  require(shape != nullptr, "missing exact snapshot");
  for (const auto& name : body.names)
    require(name.entity && name.entity->owner() == body.shape, "stale name owner");
  Array patches, caps;
  for (TopExp_Explorer found(shape->geometry, TopAbs_FACE); found.More(); found.Next()) {
    const auto face = TopoDS::Face(found.Current());
    require(face.Orientation() == TopAbs_FORWARD || face.Orientation() == TopAbs_REVERSED,
            "invalid face orientation");
    const double sign = face.Orientation() == TopAbs_REVERSED ? -1 : 1;
    const BRepAdaptor_Surface surface(face, true);
    if (surface.GetType() == GeomAbs_Plane) {
      const auto plane = surface.Plane();
      const auto normal = plane.Axis().Direction();
      require(std::hypot(normal.X(), normal.Y()) <= 1e-12, "nonhorizontal cap");
      caps.push_back(Object{{"z", plane.Location().Z()}, {"normal_z", sign * normal.Z()}});
      continue;
    }
    require(surface.GetType() == GeomAbs_BSplineSurface, "non-B-spline loft side");
    rectangle(face, surface);
    auto bs = surface.BSpline();  // Located copy, leaving the exact snapshot immutable.
    bs->Segment(surface.FirstUParameter(), surface.LastUParameter(), surface.FirstVParameter(),
                surface.LastVParameter());
    const auto data = kernel::from_occt(*bs).value();
    Array points, samples;
    for (const auto p : data.points()) points.push_back(Array{p.x, p.y, p.z});
    for (int i = 0; i <= 4; ++i)
      for (int j = 0; j <= 4; ++j) {
        const double u = surface.FirstUParameter() +
                         (surface.LastUParameter() - surface.FirstUParameter()) * i / 4;
        const double v = surface.FirstVParameter() +
                         (surface.LastVParameter() - surface.FirstVParameter()) * j / 4;
        gp_Pnt p;
        gp_Vec du, dv;
        surface.D1(u, v, p, du, dv);
        samples.push_back(
            Object{{"u", u}, {"v", v}, {"point", point(p)}, {"du", vec(du)}, {"dv", vec(dv)}});
      }
    patches.push_back(Object{{"degree_u", static_cast<double>(data.knots_u().degree())},
                             {"degree_v", static_cast<double>(data.knots_v().degree())},
                             {"knots_u", values(data.knots_u().knots())},
                             {"knots_v", values(data.knots_v().knots())},
                             {"points", std::move(points)},
                             {"weights", values(data.weights())},
                             {"orientation", sign},
                             {"samples", std::move(samples)}});
  }
  require(patches.size() == 8 && caps.size() == 2, "side/cap coverage");
  const auto& m = shape->measures();
  return {{"waist", waist},
          {"evaluation_ms", ms},
          {"volume_mm3", m.volume},
          {"area_mm2", m.area},
          {"solids", static_cast<double>(m.solids)},
          {"faces", static_cast<double>(m.faces)},
          {"edges", static_cast<double>(m.edges)},
          {"kernel_calls", static_cast<double>(result.kernel_calls)},
          {"cache_hits", static_cast<double>(result.cache_hits)},
          {"state_hash", body.hash->hex()},
          {"caps", std::move(caps)},
          {"patches", std::move(patches)}};
}
}  // namespace
int main(int argc, char** argv) {
  try {
    require(argc == 2, "usage: nf_loft_reference_probe fixture.nightfall");
    const auto source = read(argv[1]);
    auto document = ir::read_document(source).value();
    auto backend = kernel::make_occt_kernel().value();
    kernel_service::Evaluator evaluator(*backend, {96, 64 * 1024 * 1024});
    Array cases;
    kernel::ShapePtr retained;
    std::string saved_geometry;
    double retained_volume = 0, retained_area = 0;
    for (double waist : {1., 1.05, 1.}) {
      for (auto& p : document.params)
        if (p.name == "rotor_waist") p.value = waist;
      const auto start = std::chrono::steady_clock::now();
      const auto result = evaluator.evaluate(ir::write_document(document).value()).value();
      const double ms =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
              .count();
      if (!retained) {
        require(result.ok(), "initial build failed");
        retained = result.bodies.at("cell_vanes").shape;
        saved_geometry = geometry_bytes(retained);
        retained_volume = retained->measures().volume;
        retained_area = retained->measures().area;
      }
      cases.push_back(export_case(result, waist, ms));
    }
    evaluator.clear_cache();
    require(retained->measures().volume == retained_volume &&
                retained->measures().area == retained_area,
            "retained snapshot changed");
    require(geometry_bytes(retained) == saved_geometry, "retained B-rep bytes changed");
    require(read(argv[1]) == source, "source fixture changed");
    std::puts(ir::json::canonical(Object{{"input_sha256", ir::sha256(source).hex()},
                                         {"kernel", std::string(backend->identity())},
                                         {"cases", std::move(cases)}})
                  .c_str());
  } catch (const std::exception& error) {
    std::fprintf(stderr, "loft reference: %s\n", error.what());
    return 1;
  }
}
