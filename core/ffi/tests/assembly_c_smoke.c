/* SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
 * SPDX-License-Identifier: Apache-2.0
 *
 * C17 use of nf/assembly/solve.h: a four-bar (ground 100, crank 40, coupler 120, rocker 80 mm)
 * driven at 60 degrees closes at the closed-form position; an over-driven loop conflicts and
 * keeps the input placements; bad arguments are refused. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "nf/assembly/solve.h"

static int failures = 0;
#define CHECK(c)                                                            \
  do {                                                                      \
    if (!(c)) {                                                             \
      fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c); \
      ++failures;                                                           \
    }                                                                       \
  } while (0)

static nf_placement rot_z(double angle, double x, double y) {
  const double c = cos(angle), s = sin(angle);
  nf_placement p = {{c, -s, 0, s, c, 0, 0, 0, 1}, {x, y, 0}};
  return p;
}
static nf_frame at_x(double x) {
  nf_frame f = {{x, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  return f;
}
static nf_joint revolute(uint32_t a, double xa, uint32_t b, double xb) {
  nf_joint j;
  memset(&j, 0, sizeof j);
  j.kind = NF_JOINT_REVOLUTE;
  j.a = a;
  j.b = b;
  j.frame_a = at_x(xa);
  j.frame_b = at_x(xb);
  return j;
}

int main(void) {
  const double pi = acos(-1.0), theta = pi / 3;
  /* Closed form: crank tip C; rocker tip R on the upper intersection of |R - (100,0)| = 80 and
   * |R - C| = 120. */
  const double cx = 40 * cos(theta), cy = 40 * sin(theta);
  const double dx = 100 - cx, dy = -cy, d = sqrt(dx * dx + dy * dy);
  const double a = (120 * 120 - 80 * 80 + d * d) / (2 * d), h = sqrt(120 * 120 - a * a);
  const double mx = cx + a * dx / d, my = cy + a * dy / d;
  const double rx = mx - h * dy / d, ry = my + h * dx / d; /* the y > 0 branch */
  CHECK(ry > 0);
  const double coupler = atan2(ry - cy, rx - cx), rocker = atan2(ry, rx - 100);

  nf_occurrence occurrences[4] = {
      {1, 0, rot_z(0, 0, 0)},
      {0, 0, rot_z(theta + 0.1, 0, 0)},
      {0, 0, rot_z(coupler - 0.1, cx + 1, cy - 1)},
      {0, 0, rot_z(rocker + 0.1, 100, 0)},
  };
  nf_joint joints[4] = {revolute(0, 0, 1, 0), revolute(1, 40, 2, 0), revolute(2, 120, 3, 80),
                        revolute(0, 100, 3, 0)};
  joints[0].driven[0] = 1;
  joints[0].values[0] = theta;
  nf_placement placements[4];
  double mm[4], rad[4];
  uint8_t redundant[4], conflicting[4];
  nf_assembly_report report;
  size_t index = 0;
  uint32_t status = nf_assembly_solve(occurrences, 4, joints, 4, placements, mm, rad, redundant,
                                      conflicting, &report, &index);
  CHECK(status == NF_ASSEMBLY_OK);
  CHECK(report.solved == 1);
  CHECK(report.dof == 0);
  CHECK(report.loops == 1);
  /* The rocker's tip in world coordinates. */
  const nf_placement* r = &placements[3];
  const double tip_x = r->rotation[0] * 80 + r->translation[0];
  const double tip_y = r->rotation[3] * 80 + r->translation[1];
  CHECK(fabs(tip_x - rx) <= 1e-9 && fabs(tip_y - ry) <= 1e-9);
  for (int k = 0; k < 4; ++k) CHECK(mm[k] <= 1e-9 && rad[k] <= 1e-9);

  /* Driving the rocker as well, to an angle the loop cannot reach: a conflict. */
  joints[3].driven[0] = 1;
  joints[3].values[0] = pi / 2 + 1.0;
  status = nf_assembly_solve(occurrences, 4, joints, 4, placements, mm, rad, redundant, conflicting,
                             &report, &index);
  CHECK(status == NF_ASSEMBLY_OK);
  CHECK(report.solved == 0);
  CHECK(conflicting[0] || conflicting[1] || conflicting[2] || conflicting[3]);
  CHECK(memcmp(&placements[1], &occurrences[1].placement, sizeof placements[1]) == 0);

  /* Bad arguments. */
  CHECK(nf_assembly_solve(NULL, 4, joints, 4, placements, mm, rad, redundant, conflicting, &report,
                          &index) == NF_ASSEMBLY_INVALID_ARGUMENT);
  joints[1].b = 9;
  CHECK(nf_assembly_solve(occurrences, 4, joints, 4, placements, mm, rad, redundant, conflicting,
                          &report, &index) == NF_ASSEMBLY_UNKNOWN_OCCURRENCE);
  CHECK(index == 1);
  joints[1].b = 2;
  joints[2].frame_a.z[2] = 0;
  CHECK(nf_assembly_solve(occurrences, 4, joints, 4, placements, mm, rad, redundant, conflicting,
                          &report, &index) == NF_ASSEMBLY_DEGENERATE_FRAME);
  CHECK(index == 2);
  if (failures == 0) puts("assembly C ABI: ok");
  return failures == 0 ? 0 : 1;
}
