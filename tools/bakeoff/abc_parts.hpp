// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// The ABC half of the bake-off (RFC 0001 F13): per model, import / drill / round, with
// parameters fixed once in a committed plan so both kernels get identical inputs.

#pragma once

#include <string>
#include <vector>

#include "parts.hpp"

namespace nf::bakeoff {

struct AbcPlanRow {
  std::string model_id;
  std::string step_file;
  Vec3 bbox_min;
  Vec3 bbox_max;
  double drill_radius = 0.0;  ///< 10 % of the smaller X/Y extent
  double round_radius = 0.0;  ///< 0.5 % of the bounding-box diagonal
};

/// Builds the plan from the manifest by importing each model with `reference` (OCCT).
/// Models the reference cannot import are skipped and listed in `skipped`.
std::vector<AbcPlanRow> make_abc_plan(Kernel& reference, const std::string& manifest_csv,
                                      const std::string& step_dir,
                                      std::vector<std::string>& skipped);

void write_abc_plan(const std::string& path, const std::vector<AbcPlanRow>& plan);
std::vector<AbcPlanRow> read_abc_plan(const std::string& path);

/// Three parts per model, named abc-<id>-import / -drill / -round (categories abc_import,
/// abc_drill, abc_round), judged by consensus.
std::vector<Part> abc_parts(const std::vector<AbcPlanRow>& plan, const std::string& step_dir);

}  // namespace nf::bakeoff
