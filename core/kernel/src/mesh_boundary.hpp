// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Curve2d.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepMesh_EdgeDiscret.hxx>
#include <GeomLib.hxx>
#include <IMeshData_Curve.hxx>
#include <IMeshData_Edge.hxx>
#include <IMeshData_Face.hxx>
#include <IMeshData_Model.hxx>
#include <IMeshData_PCurve.hxx>
#include <IMeshTools_Parameters.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS.hxx>

#include <numeric>
#include <vector>

namespace nf::kernel::detail {
// OCCT's interior angular control skips frontier links. Refine shared edge samples
// against incident surface normals before any face triangulation, keeping one boundary.
class NormalControlledEdges final : public BRepMesh_EdgeDiscret {
 public:
  explicit NormalControlledEdges(std::size_t budget) : budget_(budget) {}
  // The native meshing context catches Standard_Failure; retain our precise reason.
  const char* failure() const noexcept { return failure_; }

 protected:
  Standard_Boolean performInternal(const Handle(IMeshData_Model) & model,
                                   const IMeshTools_Parameters& parameters,
                                   const Message_ProgressRange& range) override {
    if (!BRepMesh_EdgeDiscret::performInternal(model, parameters, range)) return false;
    std::size_t points = 0;
    struct Boundary {
      BRepAdaptor_Surface surface;
      BRepAdaptor_Curve2d curve;
    };
    for (int i = 0; i < model->EdgesNb(); ++i) {
      const auto& edge = model->GetEdge(i);
      const auto& curve = edge->GetCurve();
      const auto count = static_cast<std::size_t>(curve->ParametersNb());
      if (count > budget_ - points) reject("mesh boundary exceeds vertex budget");
      points += count;
      if (edge->GetDegenerated() || !edge->GetSameParam() || !edge->GetSameRange()) continue;
      std::vector<Boundary> boundaries;
      for (int j = 0; j < edge->PCurvesNb(); ++j) {
        const auto& pcurve = edge->GetPCurve(j);
        const auto& face = pcurve->GetFace()->GetFace();
        BRepAdaptor_Surface surface(face);
        if (surface.GetType() != GeomAbs_Plane)
          boundaries.push_back(
              {surface,
               BRepAdaptor_Curve2d(TopoDS::Edge(edge->GetEdge().Oriented(pcurve->GetOrientation())),
                                   face)});
      }
      if (boundaries.empty() || curve->ParametersNb() < 2) continue;
      const auto needs_split = [&](double a, double b, double middle) {
        for (const auto& boundary : boundaries) {
          gp_Dir na, nb, nm;
          const auto normal = [&](double t, gp_Dir& n) {
            return GeomLib::NormEstim(boundary.surface.Surface().Surface(), boundary.curve.Value(t),
                                      Precision::Confusion(), n) <= 1;
          };
          if (normal(a, na) && normal(b, nb) && normal(middle, nm) &&
              (na.Angle(nb) > parameters.AngleInterior || na.Angle(nm) > parameters.AngleInterior ||
               nm.Angle(nb) > parameters.AngleInterior))
            return true;
        }
        return false;
      };
      struct Sample {
        gp_Pnt point;
        double parameter;
      };
      std::vector<Sample> samples{{curve->GetPoint(0), curve->GetParameter(0)}};
      BRepAdaptor_Curve geometry(edge->GetEdge());
      bool changed = false;
      for (int j = 1; j < curve->ParametersNb(); ++j) {
        struct Segment {
          double a, b;
        };
        std::vector<Segment> pending{{curve->GetParameter(j - 1), curve->GetParameter(j)}};
        while (!pending.empty()) {
          const auto [a, b] = pending.back();
          pending.pop_back();
          const double middle = std::midpoint(a, b);
          if (needs_split(a, b, middle)) {
            if (middle == a || middle == b)
              reject("mesh boundary normal refinement exhausted parameter range");
            if (++points > budget_) reject("mesh boundary exceeds vertex budget");
            changed = true;
            pending.push_back({middle, b});
            pending.push_back({a, middle});
          } else {
            samples.push_back(
                {b == curve->GetParameter(j) ? curve->GetPoint(j) : geometry.Value(b), b});
          }
        }
      }
      if (!changed) continue;
      edge->Clear(false);
      for (const auto& sample : samples) curve->AddPoint(sample.point, sample.parameter);
      BRepMesh_EdgeDiscret::Tessellate2d(edge, true);
    }
    return true;
  }

 private:
  [[noreturn]] void reject(const char* message) {
    failure_ = message;
    throw Standard_Failure(message);
  }
  std::size_t budget_;
  const char* failure_ = nullptr;
};
}  // namespace nf::kernel::detail
