// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "assembly.hpp"

namespace nf::kernel_service {
/// An RFC 0013 URDF export: the URDF text, one OBJ mesh per occurrence (path relative to the
/// URDF file, text in metres in the occurrence's own frame), the joints left out because they
/// close a loop, and warnings for values URDF requires but the document does not give.
struct UrdfExport {
  std::string urdf;
  std::vector<std::pair<std::string, std::string>> meshes;
  std::vector<std::string> cut_joints, warnings;
};

/// Writes `result` as URDF. Links are occurrences, ordered from the first grounded occurrence
/// by a spanning tree of the joints in document order; a joint between two linked occurrences
/// closes a loop and is cut. A child link's frame is its joint frame, so the file does not
/// depend on the pose it was exported at: driven to a joint's value, URDF reproduces the
/// solver's placement. Revolute joints become `revolute` (with limits) or `continuous`,
/// sliders `prismatic`, rigid joints `fixed`; other kinds in the tree are refused. Mass and
/// inertia come from `density` (kg/m^3 per occurrence); without one 1000 kg/m^3 is used, with
/// a warning. Effort and velocity limits are defaults, with a warning. `mesh_dir` prefixes mesh
/// paths; `name` is the robot name.
std::expected<UrdfExport, ir::Errors> export_urdf(kernel::IKernel& kernel,
                                                  const ir::Document& document,
                                                  const AssemblyResult& result,
                                                  const std::map<std::string, double>& density,
                                                  const std::string& mesh_dir,
                                                  const std::string& name);
}  // namespace nf::kernel_service
