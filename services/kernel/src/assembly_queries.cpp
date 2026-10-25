// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "assembly_queries.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace nf::kernel_service {
namespace {
ir::Errors kernel_error(const kernel::KernelError& e, const std::string& what) {
  return {{e.code, what + ": " + e.message, "", {}}};
}
/// Placed bounding boxes meet, with room for contact.
bool boxes_meet(const kernel::Measures& p, const kernel::Measures& q) {
  constexpr double slack = kContactDistance;
  return p.minimum.x <= q.maximum.x + slack && q.minimum.x <= p.maximum.x + slack &&
         p.minimum.y <= q.maximum.y + slack && q.minimum.y <= p.maximum.y + slack &&
         p.minimum.z <= q.maximum.z + slack && q.minimum.z <= p.maximum.z + slack;
}
}  // namespace

std::string occurrence_of(const std::string& body) { return body.substr(0, body.find('/')); }

std::expected<std::vector<Interference>, ir::Errors> check_interference(
    kernel::IKernel& kernel, const std::vector<PlacedBody>& bodies,
    const std::vector<std::string>& only, std::size_t* candidates) {
  const auto wanted = [&](const std::string& body) {
    return only.empty() || std::ranges::find(only, occurrence_of(body)) != only.end();
  };
  std::vector<Interference> found;
  std::size_t checked = 0;
  for (std::size_t i = 0; i < bodies.size(); ++i)
    for (std::size_t j = i + 1; j < bodies.size(); ++j) {
      const auto& [a, shape_a] = bodies[i];
      const auto& [b, shape_b] = bodies[j];
      if (occurrence_of(a) == occurrence_of(b) || !(wanted(a) || wanted(b)) ||
          !boxes_meet(shape_a->measures(), shape_b->measures()))
        continue;
      ++checked;
      const auto p = kernel.proximity(shape_a, shape_b);
      if (!p) return std::unexpected(kernel_error(p.error(), "interference of " + a + " and " + b));
      if (p->common_volume > kOverlapVolume || p->distance <= kContactDistance)
        found.push_back({std::min(a, b), std::max(a, b),
                         p->common_volume > kOverlapVolume ? p->common_volume : 0.0,
                         p->common_volume <= kOverlapVolume});
    }
  std::ranges::sort(
      found, [](const auto& x, const auto& y) { return std::tie(x.a, x.b) < std::tie(y.a, y.b); });
  if (candidates) *candidates = checked;
  return found;
}

std::expected<Clearance, ir::Errors> measure_clearance(kernel::IKernel& kernel,
                                                       const std::vector<PlacedBody>& a,
                                                       const std::vector<PlacedBody>& b) {
  if (a.empty() || b.empty())
    return std::unexpected(
        ir::Errors{{"unknown_id", "clearance needs live bodies on both sides", "", {}}});
  Clearance best{std::numeric_limits<double>::infinity(), {}, {}, {}, {}};
  for (const auto& [ida, sa] : a)
    for (const auto& [idb, sb] : b) {
      const auto p = kernel.proximity(sa, sb);
      if (!p)
        return std::unexpected(kernel_error(p.error(), "clearance of " + ida + " and " + idb));
      if (p->distance < best.distance) best = {p->distance, ida, idb, p->on_a, p->on_b};
    }
  return best;
}
namespace {
/// Adds `part` (volume or mass `weight`, centroid, inertia about it) into `sum`, keeping the
/// inertia about the running centroid: I = I_1 + I_2 + w_1 w_2 / (w_1 + w_2) (|d|^2 E - d d^T),
/// with d the vector between the two centroids. `scale` converts weight x mm^2 to inertia units.
void accumulate(MassReport& sum, double& weight, double part_weight, const math::Vec3& centroid,
                const std::array<double, 9>& inertia, double scale) {
  if (weight == 0) {
    weight = part_weight;
    sum.centroid = centroid;
    sum.inertia = inertia;
    return;
  }
  // Divide before multiplying: two huge finite weights must not overflow.
  const double total = weight + part_weight, reduced = weight * (part_weight / total);
  const std::array<double, 3> d{centroid.x - sum.centroid.x, centroid.y - sum.centroid.y,
                                centroid.z - sum.centroid.z};
  const double d2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
  for (std::size_t i = 0; i < 3; ++i)
    for (std::size_t j = 0; j < 3; ++j)
      sum.inertia[3 * i + j] +=
          inertia[3 * i + j] + reduced * scale * ((i == j ? d2 : 0.0) - d[i] * d[j]);
  const double t = part_weight / total;
  sum.centroid = {sum.centroid.x + t * d[0], sum.centroid.y + t * d[1], sum.centroid.z + t * d[2]};
  weight = total;
}
constexpr double kMassPerDensityVolume = 1e-9;  // kg per (kg/m^3 x mm^3)
constexpr double kInertiaPerMassArea = 1e-6;    // kg m^2 per (kg x mm^2)
}  // namespace

std::expected<AssemblyMass, ir::Errors> mass_properties(
    kernel::IKernel& kernel, const std::vector<PlacedBody>& bodies,
    const std::map<std::string, double>& density, const std::vector<std::string>& only) {
  AssemblyMass result;
  std::map<std::string, std::size_t> index;
  std::map<std::string, double> weights;  // mass, or volume without a density
  for (const auto& [id, shape] : bodies) {
    const auto occurrence = occurrence_of(id);
    if (!only.empty() && std::ranges::find(only, occurrence) == only.end()) continue;
    const auto m = kernel.mass_properties(shape);
    if (!m) return std::unexpected(kernel_error(m.error(), "mass properties of " + id));
    auto [slot, added] = index.try_emplace(occurrence, result.occurrences.size());
    if (added) {
      MassReport report{occurrence, 0, std::nullopt, {}, 0, {}};
      if (const auto d = density.find(occurrence); d != density.end()) report.density = d->second;
      result.occurrences.push_back(report);
    }
    auto& report = result.occurrences[slot->second];
    // Volume-weighted unit-density inertia (mm^5) per body, scaled to kg m^2 once at the end.
    accumulate(report, weights[occurrence], m->volume, m->centroid, m->inertia, 1.0);
    report.volume += m->volume;
  }
  if (result.occurrences.empty())
    return std::unexpected(ir::Errors{{"unknown_id", "no live bodies to measure", "", {}}});
  result.total.id = "assembly";
  double total_weight = 0;
  for (auto& report : result.occurrences) {
    if (!report.density) {
      result.without_density.push_back(report.id);
      report.inertia = {};
      continue;
    }
    const double rho = *report.density;
    // Units first: a huge density must not overflow before the 1e-9 conversion.
    report.mass = rho * (report.volume * kMassPerDensityVolume);
    for (auto& entry : report.inertia)
      entry = rho * (entry * kMassPerDensityVolume * kInertiaPerMassArea);
    result.total.volume += report.volume;
    result.total.mass += report.mass;
    accumulate(result.total, total_weight, report.mass, report.centroid, report.inertia,
               kInertiaPerMassArea);
  }
  if (result.total.mass > 0)
    result.total.density = result.total.mass / (result.total.volume * kMassPerDensityVolume);
  // A density that is finite but absurd can still overflow; report it, never a non-number.
  const auto finite = [](const MassReport& r) {
    return std::isfinite(r.mass) && std::isfinite(r.volume) && std::isfinite(r.centroid.x) &&
           std::isfinite(r.centroid.y) && std::isfinite(r.centroid.z) &&
           std::ranges::all_of(r.inertia, [](double v) { return std::isfinite(v); });
  };
  if (!finite(result.total) || !std::ranges::all_of(result.occurrences, finite))
    return std::unexpected(
        ir::Errors{{"invalid_value", "mass properties overflow; check the densities", "", {}}});
  return result;
}
}  // namespace nf::kernel_service
