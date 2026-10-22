// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <expected>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "nf/ir/document.hpp"
#include "nf/kernel/kernel.hpp"

namespace nf::kernel_service {
/// A placed body addressed "occurrence/body" (RFC 0013), at its solved placement.
using PlacedBody = std::pair<std::string, kernel::ShapePtr>;

/// Two bodies of different occurrences that overlap or touch.
struct Interference {
  std::string a, b;       // body ids, a < b
  double volume = 0;      // common volume, mm^3; 0 when they only touch
  bool touching = false;  // in contact with no common volume
};
/// RFC 0013 interference: bodies of different occurrences whose placed bounding boxes meet
/// are checked exactly with IKernel::proximity. A pair overlaps when its common volume exceeds
/// kOverlapVolume, and touches when it does not but its distance is within kContactDistance.
/// `only`, when not empty, keeps the pairs with at least one body of those occurrences.
/// `candidates`, when given, receives the number of pairs checked exactly.
inline constexpr double kOverlapVolume = 1e-6;    // mm^3
inline constexpr double kContactDistance = 1e-6;  // mm
std::expected<std::vector<Interference>, ir::Errors> check_interference(
    kernel::IKernel& kernel, const std::vector<PlacedBody>& bodies,
    const std::vector<std::string>& only = {}, std::size_t* candidates = nullptr);

/// The smallest distance between two sets of placed bodies, with the bodies and closest points.
struct Clearance {
  double distance = 0;  // mm; 0 when they touch or overlap
  std::string a, b;     // the closest bodies
  math::Vec3 on_a, on_b;
};
std::expected<Clearance, ir::Errors> measure_clearance(kernel::IKernel& kernel,
                                                       const std::vector<PlacedBody>& a,
                                                       const std::vector<PlacedBody>& b);

/// RFC 0013 mass properties of one occurrence, or of the assembly, in the assembly frame.
/// Mass and inertia need a density; without one only volume and centroid are known.
struct MassReport {
  std::string id;
  double volume = 0;                // mm^3
  std::optional<double> density;    // kg/m^3
  math::Vec3 centroid;              // centre of mass (centre of volume without density), mm
  double mass = 0;                  // kg
  std::array<double, 9> inertia{};  // kg m^2 about the centroid, assembly axes, row-major
};
struct AssemblyMass {
  std::vector<MassReport> occurrences;  // in body order
  MassReport total;                     // the occurrences with a density
  std::vector<std::string> without_density;
};
/// Combines each occurrence's bodies, and then the occurrences with a density, by the
/// parallel-axis theorem. `density` maps occurrence ids to kg/m^3; `only`, when not empty,
/// keeps those occurrences.
std::expected<AssemblyMass, ir::Errors> mass_properties(
    kernel::IKernel& kernel, const std::vector<PlacedBody>& bodies,
    const std::map<std::string, double>& density, const std::vector<std::string>& only = {});

/// The occurrence part of an "occurrence/body" id.
std::string occurrence_of(const std::string& body);
}  // namespace nf::kernel_service
