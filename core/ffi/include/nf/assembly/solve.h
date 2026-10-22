/* SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Plain-C entry to Nightfall's assembly joint solver (RFC 0013, crate nightfall-assembly). The
 * caller owns every array; nothing is allocated across this boundary. Units are millimetres and
 * radians. Thread-safe: each call is independent.
 */
#ifndef NF_ASSEMBLY_SOLVE_H
#define NF_ASSEMBLY_SOLVE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Joint kinds and their free coordinates, in order (values, driven, limits):
 *   RIGID none; REVOLUTE angle about z; SLIDER offset along z; CYLINDRICAL angle, z offset;
 *   PIN_SLOT angle, x offset; PLANAR x offset, y offset, angle; BALL none (free only). */
enum {
  NF_JOINT_RIGID = 0,
  NF_JOINT_REVOLUTE = 1,
  NF_JOINT_SLIDER = 2,
  NF_JOINT_CYLINDRICAL = 3,
  NF_JOINT_PIN_SLOT = 4,
  NF_JOINT_PLANAR = 5,
  NF_JOINT_BALL = 6
};

/* Results of nf_assembly_solve; *error_index names the occurrence (INVALID_PLACEMENT) or the
 * joint (every other input error), or is SIZE_MAX. */
enum {
  NF_ASSEMBLY_OK = 0,
  NF_ASSEMBLY_INVALID_ARGUMENT = 1, /* null array with a nonzero count, unknown kind, sizes */
  NF_ASSEMBLY_INVALID_VALUE = 2,    /* non-finite value or limit, value outside its limits */
  NF_ASSEMBLY_INVALID_PLACEMENT = 3,
  NF_ASSEMBLY_DEGENERATE_FRAME = 4,
  NF_ASSEMBLY_UNKNOWN_OCCURRENCE = 5, /* an occurrence index past the array, or a self-joint */
  NF_ASSEMBLY_INTERNAL = 6            /* the solver failed unexpectedly; nothing was written */
};

/* world point = rotation (row-major) * local point + translation */
typedef struct {
  double rotation[9];
  double translation[3];
} nf_placement;

/* Right-handed orthonormal axes in the occurrence's local coordinates. */
typedef struct {
  double origin[3];
  double x[3];
  double y[3];
  double z[3];
} nf_frame;

typedef struct {
  uint32_t grounded; /* 1: never moves */
  uint32_t reserved;
  nf_placement placement; /* starting point; the solution's branch follows it */
} nf_occurrence;

typedef struct {
  uint32_t kind;
  uint32_t a, b; /* occurrence indices */
  uint32_t reserved;
  nf_frame frame_a, frame_b;
  uint8_t driven[3];  /* 1: values[k] drives free coordinate k */
  uint8_t limited[3]; /* 1: lower[k] <= coordinate k <= upper[k] */
  uint8_t reserved_flags[2];
  double values[3], lower[3], upper[3];
} nf_joint;

typedef struct {
  uint32_t solved; /* 1 when every joint holds; 0 when they conflict */
  uint32_t dof;    /* remaining mobility */
  uint32_t loops;  /* independent cycles of the joint graph */
  uint32_t iterations;
} nf_assembly_report;

/* Solves from the given placements. On NF_ASSEMBLY_OK writes one placement per occurrence (the
 * inputs when report->solved is 0), each joint's largest residual in mm and rad, and per-joint
 * redundant and conflicting flags. Output arrays may be null only when their count is zero. */
uint32_t nf_assembly_solve(const nf_occurrence* occurrences, size_t occurrence_count,
                           const nf_joint* joints, size_t joint_count, nf_placement* placements,
                           double* residual_mm, double* residual_rad, uint8_t* redundant,
                           uint8_t* conflicting, nf_assembly_report* report, size_t* error_index);

#ifdef __cplusplus
}
#endif
#endif
