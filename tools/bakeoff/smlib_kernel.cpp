// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// SMLib (usd-brep SM_API) implementation of the bake-off Kernel interface.
//
// SM_API booleans and fillets modify their first operand in place (and booleans delete the
// second on success), so every operation here works on deep copies: inputs stay untouched,
// as the Kernel interface requires.

#include <SmApiBrep.h>
#include <SmApiCurves.h>
#include <SmApiGeneral.h>
#include <SmApiPrimitives.h>
#include <SmApiQueries.h>
#include <SmBSplineCurve.h>
#include <SmBrep.h>
#include <SmLine.h>
#include <SmVector3d.h>

#include <unistd.h>
#include <exception>
#include <memory>
#include <string>
#include <vector>

#include "kernel.hpp"

#if NF_BAKEOFF_WITH_USD
#include <OcctFileConverter.h>
#include <SmApiUsd.h>

#include <filesystem>

#include "step_bridge.hpp"
#endif

namespace nf::bakeoff {

namespace {

// SM_API's tightest accepted accuracies: volume [1e-8, 1e-1], area [1e-4, 1e-1] relative
// (SmApiQueries.h). Area cannot be requested at RFC 0001's 1e-6, and on curved faces it can
// miss even 1e-4 (RFC 0001 F6). SmApiBrepComputeMassProperties is no better overall.
constexpr double kVolumeAccuracy = 1e-8;
constexpr double kAreaAccuracy = 1e-4;
// SmApiCreateHelix's tightest accepted fit tolerance (mm).
constexpr double kHelixTolerance = 1e-6;

using BrepPtr = std::unique_ptr<SmBrep>;

SmVector3d to_sm(Vec3 v) { return SmVector3d(v.x, v.y, v.z); }

void check(SmApiStatus status, const char* what) {
  if (status != SM_SUCCESS)
    throw KernelError(std::string(what) + " returned " + std::to_string(status));
}

bool inside(const SmPoint3d& lo, const SmPoint3d& hi, const Region& r) {
  return lo[0] >= r.min.x && lo[1] >= r.min.y && lo[2] >= r.min.z && hi[0] <= r.max.x &&
         hi[1] <= r.max.y && hi[2] <= r.max.z;
}

class SmlibKernel final : public Kernel {
 public:
  std::string_view name() const override { return "smlib"; }
  std::string version() const override { return NF_SMLIB_VERSION; }
  // SM_API exposes no operation history; provenance exists only inside the fillet, sweep
  // and offset modules (RFC 0001 F7).
  HistorySupport history_support() const override { return HistorySupport::kNo; }

  Body box(Vec3 origin, Vec3 size) override {
    SmBrep* raw = nullptr;
    const SmApiStatus status = SmApiCreateBox(to_sm(origin), size.x, size.y, size.z, raw);
    return adopt(raw, status, "SmApiCreateBox");
  }

  Body cylinder(Vec3 base_center, double radius, double height) override {
    SmBrep* raw = nullptr;
    const SmApiStatus status = SmApiCreateCylinder(to_sm(base_center), radius, height, raw);
    return adopt(raw, status, "SmApiCreateCylinder");
  }

  Body sphere(Vec3 center, double radius) override {
    SmBrep* raw = nullptr;
    const SmApiStatus status = SmApiCreateSphere(to_sm(center), radius, raw);
    return adopt(raw, status, "SmApiCreateSphere");
  }

  Body import_step(const std::string& path) override {
#if NF_BAKEOFF_WITH_USD
    // RFC 0002: STEP -> OCCT -> .brep -> USD BrepArray -> SmBrep, healer off (RFC 0001 F13).
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / ("nf_bakeoff_" + std::to_string(::getpid()));
    fs::create_directories(dir);
    const std::string brep = (dir / "model.brep").string(), usd = (dir / "model.usdc").string();
    std::string error;
    if (!step_to_brep(path, brep, error)) throw KernelError("STEP -> .brep: " + error);
    if (!occt::ConvertOcctFileToUsd(brep, usd, error)) throw KernelError(".brep -> USD: " + error);
    std::vector<SmBrep*> breps;
    const SmStatus status = SmApiUsdImportBreps(usd.c_str(), breps, /*bHealerIsEnabled=*/FALSE);
    std::vector<BrepPtr> owned(breps.begin(), breps.end());
    fs::remove_all(dir);
    if (status != SM_SUCCESS) throw KernelError("USD -> SMLib: status " + std::to_string(status));
    if (owned.size() != 1)
      throw KernelError("USD -> SMLib: " + std::to_string(owned.size()) + " bodies");
    return store(std::move(owned.front()));
#else
    (void)path;
    throw KernelError("SMLib STEP import needs the USD path (NF_WITH_USD, RFC 0002)");
#endif
  }

  Body rotate(Body body, Vec3 point, Vec3 axis, double degrees) override {
    BrepPtr work = copy(body);  // SmApiRotate transforms in place
    check(SmApiRotate(work.get(), to_sm(point), to_sm(axis), degrees), "SmApiRotate");
    return store(std::move(work));
  }

  Body cone(Vec3 base_center, double base_radius, double top_radius, double height) override {
    SmBrep* raw = nullptr;
    const SmApiStatus status =
        SmApiCreateCone(to_sm(base_center), base_radius, top_radius, height, raw);
    return adopt(raw, status, "SmApiCreateCone");
  }

  Body torus(Vec3 center, double major_radius, double minor_radius) override {
    SmBrep* raw = nullptr;
    const SmApiStatus status = SmApiCreateTorus(to_sm(center), major_radius, minor_radius, raw);
    return adopt(raw, status, "SmApiCreateTorus");
  }

  Body extrude(const std::vector<Vec2>& profile, double base_z, double height) override {
    // SmApiCreateLinearSweep copies its curves; the segments are freed here.
    std::vector<std::unique_ptr<SmLine>> segments;
    SmTArray<SmCurve*> curves;
    for (std::size_t i = 0; i < profile.size(); ++i) {
      const Vec2 a = profile[i];
      const Vec2 b = profile[(i + 1) % profile.size()];
      SmLine* line = nullptr;
      check(SmApiCreateLineSegment(SmPoint3d(a.x, a.y, base_z), SmPoint3d(b.x, b.y, base_z), line),
            "SmApiCreateLineSegment");
      segments.emplace_back(line);
      curves.Add(line);
    }
    SmBrep* raw = nullptr;
    const SmApiStatus status =
        SmApiCreateLinearSweep(curves, SmVector3d(0, 0, 1), height, TRUE, raw);
    return adopt(raw, status, "SmApiCreateLinearSweep");
  }

  Body helix_pipe(double helix_radius, double height, double turns, double tube_radius) override {
    // SM_API fits the helix with a B-spline to within kHelixTolerance (its tightest); the
    // pipe surface itself is swept at a looser internal tolerance (RFC 0001 F11).
    SmBSplineCurve* raw_helix = nullptr;
    check(SmApiCreateHelix(SmPoint3d(0, 0, 0), height, helix_radius, helix_radius, turns, TRUE,
                           kHelixTolerance, raw_helix),
          "SmApiCreateHelix");
    // SmApiCreatePipeSweep does not document whether the result keeps the path, so the curve
    // lives as long as the kernel.
    curves_.emplace_back(raw_helix);
    SmBrep* raw = nullptr;
    const SmApiStatus status = SmApiCreatePipeSweep(tube_radius, raw_helix, TRUE, raw);
    return adopt(raw, status, "SmApiCreatePipeSweep");
  }

  Body variable_fillet(Body body, Region edges, double start_radius, double end_radius) override {
    BrepPtr work = copy(body);
    SmTArray<SmEdge*> selected = edges_in(work.get(), edges);
    if (selected.GetSize() == 0) throw KernelError("variable fillet: no edges in region");
    check(SmApiVariableRadiusFillet(work.get(), selected, start_radius, end_radius, 1, 1),
          "SmApiVariableRadiusFillet");
    return store(std::move(work));
  }

  Body boolean(BooleanOp op, Body a, Body b) override {
    BrepPtr primary = copy(a);
    BrepPtr tool = copy(b);
    SmBrep* result = nullptr;
    SmApiStatus status = SM_ERR;
    switch (op) {
      case BooleanOp::kUnion:
        status = SmApiBooleanUnion(primary.get(), tool.get(), result);
        break;
      case BooleanOp::kDifference:
        status = SmApiBooleanDifference(primary.get(), tool.get(), result);
        break;
      case BooleanOp::kIntersection:
        status = SmApiBooleanIntersection(primary.get(), tool.get(), result);
        break;
    }
    check(status, "SmApiBoolean");
    // Success: the tool was deleted by SM_API and the result aliases the primary operand.
    (void)tool.release();
    if (result != primary.get()) throw KernelError("SmApiBoolean: result does not alias operand");
    return store(std::move(primary));
  }

  Body fillet(Body body, Region edges, double radius, FilletKind kind) override {
    BrepPtr work = copy(body);
    SmTArray<SmEdge*> selected = edges_in(work.get(), edges);
    if (selected.GetSize() == 0) throw KernelError("fillet: no edges in region");
    // Cross-section 1 = circular (continuity unused); 2 = blend curve, G2.
    const bool blend = kind == FilletKind::kG2Blend;
    check(SmApiFilletEdges(work.get(), selected, radius, blend ? 2 : 1, blend ? 2 : 1, 1.0),
          "SmApiFilletEdges");
    return store(std::move(work));
  }

  Body shell(Body body, Region open_faces, double thickness) override {
    const SmBrep* source = get(body);
    SmTArray<SmFace*> all;
    check(SmApiGetFaces(const_cast<SmBrep*>(source), all), "SmApiGetFaces");
    SmTArray<const SmFace*> removed;
    for (ULONG i = 0; i < all.GetSize(); ++i) {
      SmPoint3d lo, hi;
      check(SmApiFaceBoundingBox(all[i], TRUE, lo, hi), "SmApiFaceBoundingBox");
      if (inside(lo, hi, open_faces)) removed.Add(all[i]);
    }
    if (removed.GetSize() == 0) throw KernelError("shell: no faces in region");
    SmBrep* raw = nullptr;
    // Negative distance offsets inward (SM_API_test: -1 on a 10 mm box gives 488 mm³).
    const SmApiStatus status =
        SmApiShellBrepFull(source, -thickness, TRUE, TRUE, TRUE, removed, raw);
    return adopt(raw, status, "SmApiShellBrepFull");
  }

  Measures measure(Body body) override {
    SmBrep* brep = get(body);
    Measures m;
    SmBoolean manifold = FALSE;
    check(SmApiBrepIsManifoldSolid(brep, manifold), "SmApiBrepIsManifoldSolid");
    // IsManifoldSolid alone accepts two disjoint boxes; the census separates material
    // regions from void cavities (Measures::valid: one solid, no voids).
    long solids = 0, voids = 0;
    check(SmApiBrepMaterialCensus(brep, solids, voids), "SmApiBrepMaterialCensus");
    m.valid = manifold == TRUE && solids == 1 && voids == 0;
    check(SmApiBrepComputeVolume(brep, kVolumeAccuracy, m.volume), "SmApiBrepComputeVolume");
    check(SmApiBrepComputeArea(brep, kAreaAccuracy, m.area), "SmApiBrepComputeArea");
    SmTArray<SmFace*> faces;
    SmTArray<SmEdge*> edges;
    check(SmApiGetFaces(brep, faces), "SmApiGetFaces");
    check(SmApiGetEdges(brep, edges), "SmApiGetEdges");
    m.faces = static_cast<int>(faces.GetSize());
    m.edges = static_cast<int>(edges.GetSize());
    SmPoint3d lo, hi;
    if (SmApiBrepBoundingBox(brep, TRUE, lo, hi) == SM_SUCCESS) {
      m.bbox_min = {lo[0], lo[1], lo[2]};
      m.bbox_max = {hi[0], hi[1], hi[2]};
    }
    return m;
  }

 private:
  static SmTArray<SmEdge*> edges_in(SmBrep* brep, const Region& region) {
    SmTArray<SmEdge*> all, selected;
    check(SmApiGetEdges(brep, all), "SmApiGetEdges");
    for (ULONG i = 0; i < all.GetSize(); ++i) {
      SmPoint3d lo, hi;
      check(SmApiEdgeBoundingBox(all[i], TRUE, lo, hi), "SmApiEdgeBoundingBox");
      if (inside(lo, hi, region)) selected.Add(all[i]);
    }
    return selected;
  }

  Body adopt(SmBrep* raw, SmApiStatus status, const char* what) {
    BrepPtr owned(raw);
    check(status, what);
    if (!owned) throw KernelError(std::string(what) + " produced no body");
    return store(std::move(owned));
  }

  BrepPtr copy(Body body) {
    SmBrep* raw = nullptr;
    const SmApiStatus status = SmApiBrepCopy(get(body), raw);
    BrepPtr owned(raw);
    check(status, "SmApiBrepCopy");
    return owned;
  }

  Body store(BrepPtr brep) {
    bodies_.push_back(std::move(brep));
    return Body{static_cast<int>(bodies_.size()) - 1};
  }

  SmBrep* get(Body body) const {
    if (body.id < 0 || static_cast<std::size_t>(body.id) >= bodies_.size()) {
      throw KernelError("invalid body handle");
    }
    return bodies_[static_cast<std::size_t>(body.id)].get();
  }

  // Declared first so they are destroyed after the bodies that may reference them.
  std::vector<std::unique_ptr<SmBSplineCurve>> curves_;
  std::vector<BrepPtr> bodies_;
};

}  // namespace

std::unique_ptr<Kernel> make_smlib_kernel() { return std::make_unique<SmlibKernel>(); }

}  // namespace nf::bakeoff
