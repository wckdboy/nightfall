// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "nf/kernel/occt_geom.hpp"

#include <cmath>
#include <optional>
#include <span>
#include <vector>

#include <Standard_Failure.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColStd_Array2OfReal.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TColgp_Array1OfPnt2d.hxx>
#include <TColgp_Array2OfPnt.hxx>

namespace nf::kernel {

namespace {

using math::KnotVector;
using math::Vec2;
using math::Vec3;

// A flat knot vector as OCCT's distinct knots and multiplicities (1-based arrays).
struct OcctKnots {
  TColStd_Array1OfReal knots;
  TColStd_Array1OfInteger mults;
};

// OCCT requires distinct knots to differ by more than Epsilon of the previous one.
std::optional<std::string> occt_spacing(const KnotVector& kv) {
  for (std::size_t i = 1; i < kv.size(); ++i) {
    if (kv[i] != kv[i - 1] && std::abs(kv[i] - kv[i - 1]) <= Epsilon(std::abs(kv[i - 1]))) {
      return "knots " + std::to_string(i - 1) + " and " + std::to_string(i) +
             " are distinct but closer than OCCT can represent";
    }
  }
  return std::nullopt;
}

// Runs an OCCT construction, turning its exceptions into errors.
template <typename F>
auto guarded(F make) -> std::expected<decltype(make()), std::string> {
  try {
    return make();
  } catch (const Standard_Failure& e) {
    return std::unexpected(std::string("OCCT: ") + e.GetMessageString());
  }
}

OcctKnots to_occt_knots(const KnotVector& kv) {
  std::vector<double> values;
  std::vector<int> mults;
  for (const double k : kv.knots()) {
    if (!values.empty() && values.back() == k) {
      ++mults.back();
    } else {
      values.push_back(k);
      mults.push_back(1);
    }
  }
  OcctKnots out{TColStd_Array1OfReal(1, static_cast<int>(values.size())),
                TColStd_Array1OfInteger(1, static_cast<int>(mults.size()))};
  for (std::size_t i = 0; i < values.size(); ++i) {
    out.knots.SetValue(static_cast<int>(i) + 1, values[i]);
    out.mults.SetValue(static_cast<int>(i) + 1, mults[i]);
  }
  return out;
}

std::expected<KnotVector, std::string> from_occt_knots(const TColStd_Array1OfReal& knots,
                                                       const TColStd_Array1OfInteger& mults,
                                                       int degree) {
  if (degree > KnotVector::kMaxDegree) {
    return std::unexpected("degree " + std::to_string(degree) + " exceeds libnf-math's maximum");
  }
  std::vector<double> flat;
  for (int i = knots.Lower(); i <= knots.Upper(); ++i) {
    flat.insert(flat.end(), static_cast<std::size_t>(mults.Value(i)), knots.Value(i));
  }
  return KnotVector::make(std::move(flat), degree);
}

// Whether the end knots have full multiplicity (degree + 1), as libnf-math requires.
bool clamped(const TColStd_Array1OfInteger& mults, int degree) {
  return mults.Value(mults.Lower()) == degree + 1 && mults.Value(mults.Upper()) == degree + 1;
}

TColStd_Array1OfReal weights_1d(std::span<const double> w) {
  TColStd_Array1OfReal out(1, static_cast<int>(w.size()));
  for (std::size_t i = 0; i < w.size(); ++i) out.SetValue(static_cast<int>(i) + 1, w[i]);
  return out;
}

}  // namespace

std::expected<Handle(Geom_BSplineCurve), std::string> to_occt(const math::NurbsCurve3& c) {
  if (auto e = occt_spacing(c.knots())) return std::unexpected(*e);
  const auto k = to_occt_knots(c.knots());
  TColgp_Array1OfPnt poles(1, static_cast<int>(c.points().size()));
  for (std::size_t i = 0; i < c.points().size(); ++i) {
    const Vec3 p = c.points()[i];
    poles.SetValue(static_cast<int>(i) + 1, gp_Pnt(p.x, p.y, p.z));
  }
  return guarded([&]() -> Handle(Geom_BSplineCurve) {
    if (!c.rational()) return new Geom_BSplineCurve(poles, k.knots, k.mults, c.degree());
    return new Geom_BSplineCurve(poles, weights_1d(c.weights()), k.knots, k.mults, c.degree());
  });
}

std::expected<Handle(Geom2d_BSplineCurve), std::string> to_occt(const math::NurbsCurve2& c) {
  if (auto e = occt_spacing(c.knots())) return std::unexpected(*e);
  const auto k = to_occt_knots(c.knots());
  TColgp_Array1OfPnt2d poles(1, static_cast<int>(c.points().size()));
  for (std::size_t i = 0; i < c.points().size(); ++i) {
    poles.SetValue(static_cast<int>(i) + 1, gp_Pnt2d(c.points()[i].x, c.points()[i].y));
  }
  return guarded([&]() -> Handle(Geom2d_BSplineCurve) {
    if (!c.rational()) return new Geom2d_BSplineCurve(poles, k.knots, k.mults, c.degree());
    return new Geom2d_BSplineCurve(poles, weights_1d(c.weights()), k.knots, k.mults, c.degree());
  });
}

std::expected<Handle(Geom_BSplineSurface), std::string> to_occt(const math::NurbsSurface& s) {
  if (auto e = occt_spacing(s.knots_u())) return std::unexpected(*e);
  if (auto e = occt_spacing(s.knots_v())) return std::unexpected(*e);
  const auto ku = to_occt_knots(s.knots_u()), kv = to_occt_knots(s.knots_v());
  const int n = s.count_u(), m = s.count_v();
  TColgp_Array2OfPnt poles(1, n, 1, m);
  TColStd_Array2OfReal weights(1, n, 1, m);
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j < m; ++j) {
      const auto idx = static_cast<std::size_t>(i * m + j);
      const Vec3 p = s.points()[idx];
      poles.SetValue(i + 1, j + 1, gp_Pnt(p.x, p.y, p.z));
      weights.SetValue(i + 1, j + 1, s.weights()[idx]);
    }
  }
  const int p = s.knots_u().degree(), q = s.knots_v().degree();
  return guarded([&]() -> Handle(Geom_BSplineSurface) {
    if (!s.rational()) {
      return new Geom_BSplineSurface(poles, ku.knots, kv.knots, ku.mults, kv.mults, p, q);
    }
    return new Geom_BSplineSurface(poles, weights, ku.knots, kv.knots, ku.mults, kv.mults, p, q);
  });
}

std::expected<math::NurbsCurve3, std::string> from_occt(const Geom_BSplineCurve& curve) {
  Handle(Geom_BSplineCurve) c = Handle(Geom_BSplineCurve)::DownCast(curve.Copy());
  // A periodic (or otherwise unclamped) B-spline is re-segmented over its whole range, which
  // inserts knots up to full multiplicity at both ends without changing the curve.
  if (c->IsPeriodic()) c->SetNotPeriodic();
  if (!clamped(c->Multiplicities(), c->Degree()))
    c->Segment(c->FirstParameter(), c->LastParameter());
  auto kv = from_occt_knots(c->Knots(), c->Multiplicities(), c->Degree());
  if (!kv) return std::unexpected(kv.error());
  std::vector<Vec3> points;
  std::vector<double> weights;
  for (int i = 1; i <= c->NbPoles(); ++i) {
    const gp_Pnt p = c->Pole(i);
    points.push_back({p.X(), p.Y(), p.Z()});
    if (c->IsRational()) weights.push_back(c->Weight(i));
  }
  return math::NurbsCurve3::make(std::move(*kv), std::move(points), std::move(weights));
}

std::expected<math::NurbsCurve2, std::string> from_occt(const Geom2d_BSplineCurve& curve) {
  Handle(Geom2d_BSplineCurve) c = Handle(Geom2d_BSplineCurve)::DownCast(curve.Copy());
  if (c->IsPeriodic()) c->SetNotPeriodic();
  if (!clamped(c->Multiplicities(), c->Degree()))
    c->Segment(c->FirstParameter(), c->LastParameter());
  auto kv = from_occt_knots(c->Knots(), c->Multiplicities(), c->Degree());
  if (!kv) return std::unexpected(kv.error());
  std::vector<Vec2> points;
  std::vector<double> weights;
  for (int i = 1; i <= c->NbPoles(); ++i) {
    const gp_Pnt2d p = c->Pole(i);
    points.push_back({p.X(), p.Y()});
    if (c->IsRational()) weights.push_back(c->Weight(i));
  }
  return math::NurbsCurve2::make(std::move(*kv), std::move(points), std::move(weights));
}

std::expected<math::NurbsSurface, std::string> from_occt(const Geom_BSplineSurface& surface) {
  Handle(Geom_BSplineSurface) s = Handle(Geom_BSplineSurface)::DownCast(surface.Copy());
  if (s->IsUPeriodic()) s->SetUNotPeriodic();
  if (s->IsVPeriodic()) s->SetVNotPeriodic();
  if (!clamped(s->UMultiplicities(), s->UDegree()) ||
      !clamped(s->VMultiplicities(), s->VDegree())) {
    double u1 = 0, u2 = 0, v1 = 0, v2 = 0;
    s->Bounds(u1, u2, v1, v2);
    s->Segment(u1, u2, v1, v2);
  }
  auto ku = from_occt_knots(s->UKnots(), s->UMultiplicities(), s->UDegree());
  if (!ku) return std::unexpected(ku.error());
  auto kv = from_occt_knots(s->VKnots(), s->VMultiplicities(), s->VDegree());
  if (!kv) return std::unexpected(kv.error());
  const bool rational = s->IsURational() || s->IsVRational();
  std::vector<Vec3> points;
  std::vector<double> weights;
  for (int i = 1; i <= s->NbUPoles(); ++i) {
    for (int j = 1; j <= s->NbVPoles(); ++j) {
      const gp_Pnt p = s->Pole(i, j);
      points.push_back({p.X(), p.Y(), p.Z()});
      if (rational) weights.push_back(s->Weight(i, j));
    }
  }
  return math::NurbsSurface::make(std::move(*ku), std::move(*kv), std::move(points),
                                  std::move(weights));
}

}  // namespace nf::kernel
