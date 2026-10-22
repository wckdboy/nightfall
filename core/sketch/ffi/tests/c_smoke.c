/* SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
 * SPDX-License-Identifier: Apache-2.0
 *
 * C17 use of nf/sketch/solve.h: a rough right angle solves exactly, a contradiction is
 * reported per constraint, and bad arguments are refused without writing outputs. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "nf/sketch/solve.h"

static int failures = 0;
#define CHECK(c)                                                            \
  do {                                                                      \
    if (!(c)) {                                                             \
      fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c); \
      ++failures;                                                           \
    }                                                                       \
  } while (0)

int main(void) {
  /* Two lines from a fixed point: one horizontal, one perpendicular to it, both 10 mm. */
  nf_sketch_entity entities[3] = {
      {NF_SKETCH_POINT, 0, {0, 0, 0, 0, 0}},
      {NF_SKETCH_LINE, 0, {0.2, -0.1, 9.0, 0.6, 0}},
      {NF_SKETCH_LINE, 0, {0.1, 0.2, 0.7, 9.4, 0}},
  };
  nf_sketch_constraint constraints[8] = {
      {NF_SKETCH_FIXED, 1, {{0, NF_SKETCH_WHOLE}}, 0},
      {NF_SKETCH_COINCIDENT, 2, {{1, NF_SKETCH_START}, {0, NF_SKETCH_WHOLE}}, 0},
      {NF_SKETCH_COINCIDENT, 2, {{2, NF_SKETCH_START}, {0, NF_SKETCH_WHOLE}}, 0},
      {NF_SKETCH_HORIZONTAL, 1, {{1, NF_SKETCH_WHOLE}}, 0},
      {NF_SKETCH_PERPENDICULAR, 2, {{1, NF_SKETCH_WHOLE}, {2, NF_SKETCH_WHOLE}}, 0},
      {NF_SKETCH_DISTANCE, 2, {{1, NF_SKETCH_START}, {1, NF_SKETCH_END}}, 10},
      {NF_SKETCH_DISTANCE, 2, {{2, NF_SKETCH_START}, {2, NF_SKETCH_END}}, 10},
      {NF_SKETCH_EQUAL, 2, {{1, NF_SKETCH_WHOLE}, {2, NF_SKETCH_WHOLE}}, 0},
  };
  nf_sketch_entity solved[3];
  uint8_t under[3], failing[8], redundant[8];
  nf_sketch_report report;
  size_t index = 0;
  uint32_t status = nf_sketch_solve(entities, 3, constraints, 8, solved, under, failing, redundant,
                                    &report, &index);
  CHECK(status == NF_SKETCH_OK);
  CHECK(report.solved == 1);
  CHECK(report.dof == 0);
  CHECK(fabs(solved[1].values[2] - 10) <= 1e-9 && fabs(solved[1].values[3]) <= 1e-9);
  CHECK(fabs(solved[2].values[2]) <= 1e-9 && fabs(solved[2].values[3] - 10) <= 1e-9);
  CHECK(redundant[7] == 1); /* equal lengths follow from the two distances */
  for (int i = 0; i < 3; ++i) CHECK(under[i] == 0);

  /* Contradiction: the same two points 5 and 7 mm apart. */
  constraints[6].value = 12;
  constraints[7].kind = NF_SKETCH_DISTANCE;
  constraints[7].refs[0] = (nf_sketch_ref){1, NF_SKETCH_START};
  constraints[7].refs[1] = (nf_sketch_ref){1, NF_SKETCH_END};
  constraints[7].value = 7;
  status = nf_sketch_solve(entities, 3, constraints, 8, solved, under, failing, redundant, &report,
                           &index);
  CHECK(status == NF_SKETCH_OK);
  CHECK(report.solved == 0);
  CHECK(failing[5] == 1 || failing[7] == 1);
  CHECK(memcmp(&solved[1], &entities[1], sizeof solved[1]) == 0);

  /* Bad arguments: a null array with a nonzero count, an unknown entity, a bad kind. */
  CHECK(nf_sketch_solve(NULL, 3, constraints, 8, solved, under, failing, redundant, &report,
                        &index) == NF_SKETCH_INVALID_ARGUMENT);
  constraints[0].refs[0].entity = 9;
  CHECK(nf_sketch_solve(entities, 3, constraints, 1, solved, under, failing, redundant, &report,
                        &index) == NF_SKETCH_UNKNOWN_ENTITY);
  CHECK(index == 0);
  entities[2].kind = 99;
  CHECK(nf_sketch_solve(entities, 3, NULL, 0, solved, under, NULL, NULL, &report, &index) ==
        NF_SKETCH_INVALID_ARGUMENT);
  CHECK(index == 2);
  entities[2].kind = NF_SKETCH_LINE;
  entities[2].values[0] = NAN;
  CHECK(nf_sketch_solve(entities, 3, NULL, 0, solved, under, NULL, NULL, &report, &index) ==
        NF_SKETCH_INVALID_VALUE);
  CHECK(index == 2);
  if (failures == 0) puts("sketch C ABI: ok");
  return failures == 0 ? 0 : 1;
}
