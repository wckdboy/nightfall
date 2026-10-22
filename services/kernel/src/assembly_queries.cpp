// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "assembly_queries.hpp"

#include <algorithm>
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
}  // namespace nf::kernel_service
