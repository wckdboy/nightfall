// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "abc_parts.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace nf::bakeoff {

namespace {

std::vector<std::string> split_csv_line(const std::string& line) {
  std::vector<std::string> out;
  std::stringstream ss(line);
  std::string field;
  while (std::getline(ss, field, ',')) {
    if (!field.empty() && field.back() == '\r') field.pop_back();
    out.push_back(field);
  }
  return out;
}

std::string path_of(const std::string& step_dir, const AbcPlanRow& row) {
  return step_dir + "/" + row.model_id + "/" + row.step_file;
}

}  // namespace

std::vector<AbcPlanRow> make_abc_plan(Kernel& reference, const std::string& manifest_csv,
                                      const std::string& step_dir,
                                      std::vector<std::string>& skipped) {
  std::ifstream in(manifest_csv);
  if (!in) throw std::runtime_error("cannot read " + manifest_csv);
  std::string line;
  std::getline(in, line);  // header: model_id,step_file,faces,bin,sha256
  std::vector<AbcPlanRow> plan;
  while (std::getline(in, line)) {
    const auto f = split_csv_line(line);
    if (f.size() < 2) continue;
    AbcPlanRow row{.model_id = f[0], .step_file = f[1]};
    try {
      const Measures m = reference.measure(reference.import_step(path_of(step_dir, row)));
      row.bbox_min = m.bbox_min;
      row.bbox_max = m.bbox_max;
    } catch (const std::exception&) {
      skipped.push_back(row.model_id);
      continue;
    }
    const Vec3 size = row.bbox_max - row.bbox_min;
    row.drill_radius = 0.10 * std::min(size.x, size.y);
    row.round_radius = 0.005 * math::length(size);
    plan.push_back(row);
  }
  return plan;
}

void write_abc_plan(const std::string& path, const std::vector<AbcPlanRow>& plan) {
  std::ofstream out(path);
  out << "model_id,step_file,min_x,min_y,min_z,max_x,max_y,max_z,drill_radius,round_radius\n";
  char buf[512];
  for (const AbcPlanRow& r : plan) {
    std::snprintf(buf, sizeof buf, "%s,%s,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n",
                  r.model_id.c_str(), r.step_file.c_str(), r.bbox_min.x, r.bbox_min.y, r.bbox_min.z,
                  r.bbox_max.x, r.bbox_max.y, r.bbox_max.z, r.drill_radius, r.round_radius);
    out << buf;
  }
}

std::vector<AbcPlanRow> read_abc_plan(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot read " + path);
  std::string line;
  std::getline(in, line);
  std::vector<AbcPlanRow> plan;
  while (std::getline(in, line)) {
    const auto f = split_csv_line(line);
    if (f.size() != 10) continue;
    const auto d = [&](std::size_t i) { return std::stod(f[i]); };
    plan.push_back({.model_id = f[0],
                    .step_file = f[1],
                    .bbox_min = {d(2), d(3), d(4)},
                    .bbox_max = {d(5), d(6), d(7)},
                    .drill_radius = d(8),
                    .round_radius = d(9)});
  }
  return plan;
}

std::vector<Part> abc_parts(const std::vector<AbcPlanRow>& plan, const std::string& step_dir) {
  constexpr const char* kReason = "ABC model: no analytic value (RFC 0001 F13)";
  std::vector<Part> parts;
  for (const AbcPlanRow& r : plan) {
    const std::string step = path_of(step_dir, r);
    const std::string id = "abc-" + r.model_id;
    parts.push_back({id + "-import",
                     "abc_import",
                     [step](Kernel& k) { return k.import_step(step); },
                     {.consensus_reason = kReason}});
    parts.push_back({id + "-drill",
                     "abc_drill",
                     [step, r](Kernel& k) {
                       const Body body = k.import_step(step);
                       const Vec3 c = (r.bbox_min + r.bbox_max) / 2.0;
                       const double h = r.bbox_max.z - r.bbox_min.z + 2.0;
                       const Body tool =
                           k.cylinder({c.x, c.y, r.bbox_min.z - 1.0}, r.drill_radius, h);
                       return k.boolean(BooleanOp::kDifference, body, tool);
                     },
                     {.consensus_reason = kReason}});
    parts.push_back({id + "-round",
                     "abc_round",
                     [step, r](Kernel& k) {
                       const Body body = k.import_step(step);
                       // Every edge: the region is the bounding box padded by 1 mm.
                       const Region all{r.bbox_min - Vec3{1, 1, 1}, r.bbox_max + Vec3{1, 1, 1}};
                       return k.fillet(body, all, r.round_radius, FilletKind::kCircular);
                     },
                     {.consensus_reason = kReason}});
  }
  return parts;
}

}  // namespace nf::bakeoff
