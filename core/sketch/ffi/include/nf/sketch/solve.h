/* SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Plain-C entry to Nightfall's sketch constraint solver (RFC 0012, crate nightfall-sketch-ffi).
 * The caller owns every array; nothing is allocated across this boundary. Units are
 * millimetres and radians. Thread-safe: each call is independent.
 */
#ifndef NF_SKETCH_SOLVE_H
#define NF_SKETCH_SOLVE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Entity kinds and their values:
 *   POINT  x, y
 *   LINE   x0, y0, x1, y1
 *   CIRCLE cx, cy, r
 *   ARC    cx, cy, r, start_angle, sweep_angle (negative sweep: clockwise; 0 < |sweep| < 2*pi) */
enum { NF_SKETCH_POINT = 1, NF_SKETCH_LINE = 2, NF_SKETCH_CIRCLE = 3, NF_SKETCH_ARC = 4 };

/* Which point of an entity a reference means; WHOLE is the entity itself. */
enum { NF_SKETCH_WHOLE = 0, NF_SKETCH_START = 1, NF_SKETCH_END = 2, NF_SKETCH_CENTER = 3 };

/* RFC 0005 v1 constraints. ref_count references are used; value for DISTANCE (mm), ANGLE
 * (rad) and RADIUS (mm). SYMMETRIC is (point, point, axis line); MIDPOINT takes a point and a
 * line in either order. */
enum {
  NF_SKETCH_COINCIDENT = 1,
  NF_SKETCH_HORIZONTAL,
  NF_SKETCH_VERTICAL,
  NF_SKETCH_PARALLEL,
  NF_SKETCH_PERPENDICULAR,
  NF_SKETCH_TANGENT,
  NF_SKETCH_EQUAL,
  NF_SKETCH_SYMMETRIC,
  NF_SKETCH_MIDPOINT,
  NF_SKETCH_FIXED,
  NF_SKETCH_DISTANCE,
  NF_SKETCH_ANGLE,
  NF_SKETCH_RADIUS
};

/* Results of nf_sketch_solve. Input errors set *error_index:
 * - INVALID_ARGUMENT: the entity of an unknown entity kind; the constraint of an unknown
 *   constraint kind, role or ref_count; SIZE_MAX for a null array or a size mismatch.
 * - INVALID_VALUE: the entity (SIZE_MAX for a constraint value).
 * - UNKNOWN_ENTITY and UNSUPPORTED: the constraint.
 * - GEOMETRY: SIZE_MAX (the solver names no entity). */
enum {
  NF_SKETCH_OK = 0,
  NF_SKETCH_INVALID_ARGUMENT = 1, /* null array with a nonzero count, unknown kind or role */
  NF_SKETCH_INVALID_VALUE = 2,    /* non-finite or out-of-range number */
  NF_SKETCH_UNKNOWN_ENTITY = 3,   /* a reference index past the entity array */
  NF_SKETCH_UNSUPPORTED = 4,      /* a constraint on the wrong kind of entity */
  NF_SKETCH_GEOMETRY = 5,         /* degenerate geometry the solver refuses */
  NF_SKETCH_INTERNAL = 6          /* the solver failed unexpectedly; nothing was written */
};

typedef struct {
  uint32_t kind;
  uint32_t reserved; /* zero */
  double values[5];
} nf_sketch_entity;

typedef struct {
  uint32_t entity; /* index into the entity array */
  uint32_t role;
} nf_sketch_ref;

typedef struct {
  uint32_t kind;
  uint32_t ref_count; /* 1..3, as the kind requires */
  nf_sketch_ref refs[3];
  double value;
} nf_sketch_constraint;

typedef struct {
  uint32_t solved; /* 1 when every constraint holds; 0 when they conflict */
  uint32_t dof;    /* remaining degrees of freedom */
  uint32_t iterations;
  uint32_t reserved;
  double max_residual; /* mm or rad */
} nf_sketch_report;

/* Solves from the authored coordinates in `entities`. On NF_SKETCH_OK, writes `solved`
 * (entity_count entries; the authored values when report->solved is 0), sets
 * underconstrained[i] to 1 for entities the constraints do not determine, and sets failing[j]
 * and redundant[j] per constraint. Output arrays may be null only when their count is zero. */
uint32_t nf_sketch_solve(const nf_sketch_entity* entities, size_t entity_count,
                         const nf_sketch_constraint* constraints, size_t constraint_count,
                         nf_sketch_entity* solved, uint8_t* underconstrained, uint8_t* failing,
                         uint8_t* redundant, nf_sketch_report* report, size_t* error_index);

#ifdef __cplusplus
}
#endif
#endif
