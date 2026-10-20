// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "snapshot.hpp"

#include <BRepAdaptor_Curve2d.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp_Domain.hxx>
#include <BRepGProp_Face.hxx>
#include <BRepGProp_Sinert.hxx>
#include <BRepGProp_Vinert.hxx>
#include <BRepGProp_VinertGK.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <OSD_Parallel.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Iterator.hxx>
#include <math_Function.hxx>
#include <math_KronrodSingleIntegration.hxx>

#include <algorithm>
#include <array>
#include <limits>

namespace nf::kernel::detail {
namespace {
math::Vec3 point(const gp_Pnt& p) { return {p.X(), p.Y(), p.Z()}; }
bool finite(math::Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool solid_tree(const TopoDS_Shape& shape) {
  if (shape.ShapeType() == TopAbs_SOLID) {
    bool has_shell = false;
    for (TopExp_Explorer shells(shape, TopAbs_SHELL); shells.More(); shells.Next()) {
      has_shell = true;
      if (!BRep_Tool::IsClosed(shells.Current())) return false;
    }
    return has_shell;
  }
  if (shape.ShapeType() != TopAbs_COMPOUND && shape.ShapeType() != TopAbs_COMPSOLID) return false;
  bool has_child = false;
  for (TopoDS_Iterator child(shape); child.More(); child.Next()) {
    has_child = true;
    if (!solid_tree(child.Value())) return false;
  }
  return has_child;
}
bool linear_trims(const TopoDS_Face& face) {
  bool bounded = false;
  for (TopExp_Explorer edges(face, TopAbs_EDGE); edges.More(); edges.Next()) {
    const BRepAdaptor_Curve2d curve(TopoDS::Edge(edges.Current()), face);
    if (curve.GetType() != GeomAbs_Line || !std::isfinite(curve.FirstParameter()) ||
        !std::isfinite(curve.LastParameter()))
      return false;
    bounded = true;
  }
  return bounded;
}
bool polynomial_face(const TopoDS_Face& face) {
  return BRepAdaptor_Surface(face).GetType() == GeomAbs_Plane && linear_trims(face);
}
std::optional<double> constant_metric_area(const TopoDS_Face& input) {
  // Domain emits edges of a forward face. Use that same orientation for seam-pcurve
  // lookup, particularly on inward cylindrical faces rebuilt by mirror/unification.
  const auto face = TopoDS::Face(input.Oriented(TopAbs_FORWARD));
  const BRepAdaptor_Surface surface(face);
  if (surface.GetType() != GeomAbs_Plane && surface.GetType() != GeomAbs_Cylinder) return {};
  gp_Pnt point;
  gp_Vec du, dv;
  surface.D1(0, 0, point, du, dv);
  const auto jacobian = du.Crossed(dv).Magnitude();
  // Green's theorem in UV: 2*area = integral(u*dv - v*du). Lines and analytic
  // conic arcs have closed-form integrals; inner wires retain their signed area.
  const auto cross = [](const gp_XY& a, const gp_XY& b) {
    return static_cast<long double>(a.X()) * b.Y() - static_cast<long double>(a.Y()) * b.X();
  };
  std::optional<gp_XY> origin;
  long double twice_area = 0;
  for (BRepGProp_Domain boundary(face); boundary.More(); boundary.Next()) {
    const auto& edge = boundary.Value();
    if (edge.Orientation() != TopAbs_FORWARD && edge.Orientation() != TopAbs_REVERSED) return {};
    const BRepAdaptor_Curve2d curve(edge, face);
    const double first = curve.FirstParameter(), last = curve.LastParameter();
    if (!std::isfinite(first) || !std::isfinite(last)) return {};
    const auto a = curve.Value(first).XY(), b = curve.Value(last).XY();
    if (!origin) origin = a;
    long double integral;
    if (curve.GetType() == GeomAbs_Line) {
      integral = cross(a - *origin, b - *origin);
    } else if (curve.GetType() == GeomAbs_Circle) {
      const auto circle = curve.Circle();
      integral = cross(circle.Location().XY() - *origin, b - a) +
                 static_cast<long double>(circle.Radius()) * circle.Radius() * (last - first) *
                     (circle.IsDirect() ? 1 : -1);
    } else if (curve.GetType() == GeomAbs_Ellipse) {
      const auto ellipse = curve.Ellipse();
      integral = cross(ellipse.Location().XY() - *origin, b - a) +
                 static_cast<long double>(ellipse.MajorRadius()) * ellipse.MinorRadius() *
                     (last - first) * (ellipse.IsDirect() ? 1 : -1);
    } else {
      return {};
    }
    twice_area += edge.Orientation() == TopAbs_REVERSED ? -integral : integral;
  }
  const double area = static_cast<double>(twice_area * jacobian / 2);
  if (!(area > 0) || !std::isfinite(area)) return {};
  return area;
}
double bounded_integral(math_Function& function, double first, double last, double accuracy) {
  math_KronrodSingleIntegration integral(function, first, last, 15, accuracy, 100);
  if (!integral.IsDone() || !std::isfinite(integral.Value()) ||
      !std::isfinite(integral.ErrorReached()) || integral.ErrorReached() > accuracy)
    throw Standard_Failure("rectangular surface-area integration did not converge");
  return integral.Value();
}
std::optional<double> rectangular_area(const TopoDS_Face& input) {
  const auto face = TopoDS::Face(input.Oriented(TopAbs_FORWARD));
  const BRepAdaptor_Surface surface(face);
  if (surface.GetType() != GeomAbs_SurfaceOfExtrusion &&
      surface.GetType() != GeomAbs_BSplineSurface && surface.GetType() != GeomAbs_BezierSurface)
    return {};
  std::array<std::pair<gp_XY, gp_XY>, 4> edges;
  std::size_t count = 0;
  for (BRepGProp_Domain boundary(face); boundary.More(); boundary.Next()) {
    if (count == edges.size()) return {};
    const BRepAdaptor_Curve2d curve(boundary.Value(), face);
    if (curve.GetType() != GeomAbs_Line || !std::isfinite(curve.FirstParameter()) ||
        !std::isfinite(curve.LastParameter()))
      return {};
    edges[count++] = {curve.Value(curve.FirstParameter()).XY(),
                      curve.Value(curve.LastParameter()).XY()};
  }
  if (count != edges.size()) return {};
  double u1 = edges[0].first.X(), u2 = u1, v1 = edges[0].first.Y(), v2 = v1;
  for (const auto& [a, b] : edges)
    for (const auto& p : {a, b}) {
      u1 = std::min(u1, p.X());
      u2 = std::max(u2, p.X());
      v1 = std::min(v1, p.Y());
      v2 = std::max(v2, p.Y());
    }
  if (!(u2 > u1) || !(v2 > v1)) return {};
  unsigned sides = 0;
  for (const auto& [a, b] : edges) {
    unsigned side = 0;
    if (a.X() == b.X() && std::min(a.Y(), b.Y()) == v1 && std::max(a.Y(), b.Y()) == v2)
      side = a.X() == u1 ? 1 : a.X() == u2 ? 2 : 0;
    else if (a.Y() == b.Y() && std::min(a.X(), b.X()) == u1 && std::max(a.X(), b.X()) == u2)
      side = a.Y() == v1 ? 4 : a.Y() == v2 ? 8 : 0;
    if (!side || (sides & side)) return {};
    sides |= side;
  }
  if (sides != 15) return {};
  // The four exact UV sides prove an unperforated rectangle. Integrate the positive
  // surface metric directly, avoiding cancellation in the generic boundary integral.
  class Metric final : public math_Function {
   public:
    explicit Metric(const BRepAdaptor_Surface& s) : surface(s) {}
    double v = 0;
    Standard_Boolean Value(Standard_Real u, Standard_Real& value) override {
      gp_Pnt p;
      gp_Vec du, dv;
      surface.D1(u, v, p, du, dv);
      value = du.Crossed(dv).Magnitude();
      return value >= 0 && std::isfinite(value);
    }

   private:
    const BRepAdaptor_Surface& surface;
  } metric(surface);
  double area = 0;
  if (surface.GetType() == GeomAbs_SurfaceOfExtrusion) {
    // A linear extrusion's metric is independent of v.
    metric.v = v1;
    area = bounded_integral(metric, u1, u2, kMassRelativeAccuracy) * (v2 - v1);
  } else {
    // Split at all surface knots so each nested quadrature sees a smooth patch.
    TColStd_Array1OfReal us(1, surface.NbUIntervals(GeomAbs_CN) + 1),
        vs(1, surface.NbVIntervals(GeomAbs_CN) + 1);
    surface.UIntervals(us, GeomAbs_CN);
    surface.VIntervals(vs, GeomAbs_CN);
    class Strip final : public math_Function {
     public:
      Strip(Metric& m, const TColStd_Array1OfReal& intervals, double first, double last)
          : metric(m), us(intervals), u1(first), u2(last) {}
      Standard_Boolean Value(Standard_Real v, Standard_Real& value) override {
        metric.v = v;
        value = 0;
        for (int i = us.Lower(); i < us.Upper(); ++i) {
          const double a = std::max(u1, us(i)), b = std::min(u2, us(i + 1));
          if (b > a) value += bounded_integral(metric, a, b, kMassRelativeAccuracy / 10);
        }
        return value >= 0 && std::isfinite(value);
      }

     private:
      Metric& metric;
      const TColStd_Array1OfReal& us;
      double u1, u2;
    } strip(metric, us, u1, u2);
    for (int i = vs.Lower(); i < vs.Upper(); ++i) {
      const double a = std::max(v1, vs(i)), b = std::min(v2, vs(i + 1));
      if (b > a) area += bounded_integral(strip, a, b, kMassRelativeAccuracy / 10);
    }
  }
  if (!(area > 0) || !std::isfinite(area)) return {};
  return area;
}
bool surface_properties(const TopoDS_Shape& shape, Measures& measured) {
  BRepGProp_Sinert integral;
  integral.SetLocation(gp_Pnt(measured.centroid.x, measured.centroid.y, measured.centroid.z));
  BRepGProp_Face surface;
  for (TopExp_Explorer faces(shape, TopAbs_FACE); faces.More(); faces.Next()) {
    const auto face = TopoDS::Face(faces.Current());
    if (const auto area = constant_metric_area(face)) {
      measured.area += *area;
      continue;
    }
    if (const auto area = rectangular_area(face)) {
      measured.area += *area;
      continue;
    }
    surface.Load(face);
    BRepGProp_Domain domain(face);
    const auto error = face.NbChildren() == 0
                           ? integral.Perform(surface, kMassRelativeAccuracy)
                           : integral.Perform(surface, domain, kMassRelativeAccuracy);
    if (error < 0 || !std::isfinite(error)) return false;
    if (integral.Mass() < 0 || !std::isfinite(integral.Mass())) return false;
    measured.area += integral.Mass();
  }
  return true;
}
bool volume_properties(const TopoDS_Shape& shape, Measures& measured) {
  // A corner keeps each coordinate's first moment away from a symmetry-axis zero.
  // Use each shell's bounds so distant components and cavities integrate locally.
  // Signed face integrals use exact surfaces; non-polynomial faces keep the GK tolerance.
  struct FaceTask {
    TopoDS_Face face;
    gp_Pnt reference;
  };
  struct ShellRange {
    gp_Pnt reference;
    std::size_t first, last;
  };
  std::vector<FaceTask> tasks;
  std::vector<ShellRange> ranges;
  TopExp_Explorer shells(shape, TopAbs_SHELL);
  if (!shells.More()) return false;
  shells.Next();
  const bool single_shell = !shells.More();
  for (shells.Init(shape, TopAbs_SHELL); shells.More(); shells.Next()) {
    // A single shell already has exactly these optimal bounds. Reusing its corner
    // avoids a second global surface-extrema search on spline faces.
    gp_Pnt reference(measured.maximum.x, measured.maximum.y, measured.maximum.z);
    if (!single_shell) {
      Bnd_Box bounds;
      BRepBndLib::AddOptimal(shells.Current(), bounds, false, false);
      if (bounds.IsVoid() || bounds.IsOpen()) return false;
      reference = bounds.CornerMax();
    }
    if (!finite(point(reference))) return false;
    const auto first = tasks.size();
    for (TopExp_Explorer found(shells.Current(), TopAbs_FACE); found.More(); found.Next()) {
      // Preserve multiplicity and traversal order, including oppositely oriented faces.
      tasks.push_back({TopoDS::Face(found.Current()), reference});
    }
    ranges.push_back({reference, first, tasks.size()});
  }
  if (tasks.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return false;
  struct Contribution {
    double volume = 0;
    math::Vec3 moment;
    bool valid = false;
  };
  std::vector<Contribution> contributions(tasks.size());
  // One pool launch for the shape avoids repeated barriers on thousands of small shells.
  OSD_Parallel::For(
      0, static_cast<int>(tasks.size()),
      [&](int index) {
        const auto& [face, reference] = tasks[static_cast<std::size_t>(index)];
        auto& result = contributions[static_cast<std::size_t>(index)];
        if (face.Orientation() != TopAbs_FORWARD && face.Orientation() != TopAbs_REVERSED) {
          result.valid = true;
          return;
        }
        // Integrators/adaptors are private to a face. Only the immutable B-rep is shared.
        BRepGProp_VinertGK integral;
        integral.SetLocation(reference);
        BRepGProp_Vinert polynomial;
        polynomial.SetLocation(reference);
        BRepGProp_Face surface(true);
        surface.Load(face);
        BRepGProp_Domain domain(face);
        const GProp_GProps* properties = &integral;
        if (polynomial_face(face)) {
          // Affine plane/line parameterizations make the integrands polynomial. OCCT's
          // fixed Gaussian rule integrates them exactly (up to roundoff), avoiding
          // relative-error refinement of almost-zero face contributions.
          polynomial.Perform(surface, domain);
          properties = &polynomial;
        } else {
          const auto error =
              face.NbChildren() == 0
                  ? integral.Perform(surface, kMassRelativeAccuracy, true, false)
                  : integral.Perform(surface, domain, kMassRelativeAccuracy, true, false);
          if (error < 0 || !std::isfinite(error)) return;
        }
        result.volume = properties->Mass();
        result.moment = point(properties->CentreOfMass().XYZ() - reference.XYZ()) * result.volume;
        result.valid = true;
      },
      tasks.size() < 4);
  math::Vec3 moment;
  for (const auto& [reference, first, last] : ranges) {
    double shell_volume = 0;
    math::Vec3 shell_moment;
    for (auto i = first; i < last; ++i) {
      const auto& result = contributions[i];
      if (!result.valid) return false;
      // GProps::Add normalizes after each face and drops first moments whenever a
      // partial signed sum is zero. Keep unnormalized moments until the shell is complete.
      // Reduce in original traversal order so scheduling cannot change rounding.
      shell_volume += result.volume;
      shell_moment = shell_moment + result.moment;
    }
    measured.volume += shell_volume;
    moment = moment + shell_moment + (point(reference) - measured.maximum) * shell_volume;
  }
  if (!(measured.volume > 0)) return false;
  measured.centroid = measured.maximum + moment / measured.volume;
  return true;
}
}  // namespace

std::expected<SnapshotPtr, KernelError> snapshot(TopoDS_Shape geometry) {
  try {
    // Large patterns keep full geometric checks, distributed by OCCT's compound analyzer.
    if (geometry.IsNull() || !solid_tree(geometry) ||
        !BRepCheck_Analyzer(geometry, true, geometry.ShapeType() != TopAbs_SOLID).IsValid())
      return std::unexpected(KernelError{"kernel", "output is not valid closed solid geometry"});
    auto result = std::make_shared<Snapshot>();
    result->geometry = std::move(geometry);
    auto& m = result->measured;
    Bnd_Box bounds;
    BRepBndLib::AddOptimal(result->geometry, bounds, false, false);
    if (bounds.IsVoid() || bounds.IsOpen())
      return std::unexpected(KernelError{"kernel", "output has no finite bounds"});
    bounds.Get(m.minimum.x, m.minimum.y, m.minimum.z, m.maximum.x, m.maximum.y, m.maximum.z);
    if (!volume_properties(result->geometry, m))
      return std::unexpected(KernelError{"kernel", "volume integration failed"});
    if (!surface_properties(result->geometry, m))
      return std::unexpected(KernelError{"kernel", "surface integration failed"});
    if (!(m.volume > 0) || !(m.area > 0) || !std::isfinite(m.volume) || !std::isfinite(m.area) ||
        !finite(m.centroid) || !finite(m.minimum) || !finite(m.maximum))
      return std::unexpected(KernelError{"kernel", "output has invalid measurements"});
    TopTools_IndexedMapOfShape solids, faces, edges, vertices;
    TopExp::MapShapes(result->geometry, TopAbs_SOLID, solids);
    TopExp::MapShapes(result->geometry, TopAbs_FACE, faces);
    TopExp::MapShapes(result->geometry, TopAbs_EDGE, edges);
    TopExp::MapShapes(result->geometry, TopAbs_VERTEX, vertices);
    m.solids = static_cast<std::size_t>(solids.Extent());
    m.faces = static_cast<std::size_t>(faces.Extent());
    m.edges = static_cast<std::size_t>(edges.Extent());
    // Initial topology-weighted estimate; not a hard bound on NURBS/allocator storage.
    result->bytes =
        4096 + m.faces * 4096 + m.edges * 2048 + static_cast<std::size_t>(vertices.Extent()) * 256;
    return result;
  } catch (const Standard_Failure& e) {
    return std::unexpected(KernelError{"kernel", e.GetMessageString()});
  }
}

std::expected<SnapshotPtr, KernelError> rigid_snapshot(TopoDS_Shape geometry,
                                                       const Snapshot& source,
                                                       const gp_Trsf& transform) {
  try {
    if (!geometry.IsPartner(source.geometry) ||
        geometry.Orientation() != source.geometry.Orientation())
      return snapshot(std::move(geometry));
    const auto expected = transform * source.geometry.Location().Transformation();
    const auto actual = geometry.Location().Transformation();
    for (int row = 1; row <= 3; ++row)
      for (int column = 1; column <= 4; ++column)
        if (!std::isfinite(actual.Value(row, column)) ||
            actual.Value(row, column) != expected.Value(row, column))
          return snapshot(std::move(geometry));
    if (transform.IsNegative() || transform.ScaleFactor() != 1)
      return snapshot(std::move(geometry));
    auto result = std::make_shared<Snapshot>(source);
    result->geometry = std::move(geometry);
    auto& m = result->measured;
    m.centroid = point(gp_Pnt(m.centroid.x, m.centroid.y, m.centroid.z).Transformed(transform));
    if (transform.Form() == gp_Translation || transform.Form() == gp_Identity) {
      const auto t = transform.TranslationPart();
      m.minimum = m.minimum + math::Vec3{t.X(), t.Y(), t.Z()};
      m.maximum = m.maximum + math::Vec3{t.X(), t.Y(), t.Z()};
    } else {
      Bnd_Box bounds;
      BRepBndLib::AddOptimal(result->geometry, bounds, false, false);
      if (bounds.IsVoid() || bounds.IsOpen())
        return std::unexpected(KernelError{"kernel", "rigid output has no finite bounds"});
      bounds.Get(m.minimum.x, m.minimum.y, m.minimum.z, m.maximum.x, m.maximum.y, m.maximum.z);
    }
    if (!finite(m.centroid) || !finite(m.minimum) || !finite(m.maximum))
      return std::unexpected(KernelError{"kernel", "rigid output has nonfinite measurements"});
    return result;
  } catch (const Standard_Failure& e) {
    return std::unexpected(KernelError{"kernel", e.GetMessageString()});
  }
}

EntityKind Subshape::kind() const noexcept {
  return geometry.ShapeType() == TopAbs_FACE ? EntityKind::kFace : EntityKind::kEdge;
}
bool Subshape::same_as(const Entity& other) const noexcept {
  const auto* rhs = dynamic_cast<const Subshape*>(&other);
  return rhs && snapshot == rhs->snapshot && geometry.IsSame(rhs->geometry);
}
EntityPtr entity(SnapshotPtr owner, const TopoDS_Shape& geometry) {
  auto result = std::make_shared<Subshape>();
  result->snapshot = std::move(owner);
  result->geometry = geometry;
  return result;
}
std::vector<EntityPtr> entities(const SnapshotPtr& owner) {
  std::vector<EntityPtr> result;
  for (auto kind : {TopAbs_FACE, TopAbs_EDGE}) {
    TopTools_IndexedMapOfShape shapes;
    TopExp::MapShapes(owner->geometry, kind, shapes);
    for (int i = 1; i <= shapes.Extent(); ++i) result.push_back(entity(owner, shapes(i)));
  }
  return result;
}
}  // namespace nf::kernel::detail
