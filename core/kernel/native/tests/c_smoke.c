// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "nf_native.h"
_Static_assert(sizeof(uint32_t) == 4, "status width");
_Static_assert(offsetof(nf_native_info, identity) == 8, "identity layout");
_Static_assert(sizeof(nf_native_curve2) == 56, "curve layout");
_Static_assert(sizeof(nf_native_result_info) == 136, "result layout");
_Static_assert(sizeof(nf_native_generated) == 64, "generated layout");
_Static_assert(sizeof(nf_native_rigid_transform) == 80, "transform layout");
_Static_assert(sizeof(nf_native_history) == 32, "history layout");
_Static_assert(sizeof(nf_native_entity_view) == 16, "entity layout");
int main(void) {
  nf_native_context* context = NULL;
  uint32_t status = nf_native_context_create(NF_NATIVE_ABI_VERSION, 1e-6, 1e-9, &context);
  if (NF_EXPECT_OCCT) {
    if (status != NF_NATIVE_OK || !context) return 1;
    nf_native_info info = {0};
    if (nf_native_context_info(context, &info) != NF_NATIVE_OK || info.abi_version != 1 ||
        info.capabilities != 511 || !info.identity || !info.identity_bytes)
      return 2;
    const double knots[] = {0, 0, 1, 1};
    const nf_native_point2 points[][2] = {
        {{0, 0}, {10, 0}}, {{10, 0}, {10, 20}}, {{10, 20}, {0, 20}}, {{0, 20}, {0, 0}}};
    nf_native_curve2 curves[4];
    for (size_t i = 0; i < 4; ++i)
      curves[i] = (nf_native_curve2){1, knots, 4, points[i], 2, NULL, 0};
    const nf_native_region2 region = {{curves, 4}, NULL, 0};
    const nf_native_extrusion input = {.x_axis = {1, 0, 0},
                                       .normal = {0, 0, 1},
                                       .displacement = {0, 0, 30},
                                       .regions = &region,
                                       .region_count = 1};
    nf_native_result* result = NULL;
    if (nf_native_extrude(context, &input, &result) != NF_NATIVE_OK || !result) return 4;
    const nf_native_rigid_transform transform = {.translation = {7, 2, 3},
                                                 .axis_direction = {0, 0, 5}};
    nf_native_result* moved = NULL;
    if (nf_native_transform(context, result, &transform, &moved) != NF_NATIVE_OK || !moved)
      return 7;
    nf_native_context_release(context);
    context = NULL;
    nf_native_result_info measures = {0};
    if (nf_native_result_query(result, &measures) != NF_NATIVE_OK ||
        measures.volume < 6000 - 1e-8 || measures.volume > 6000 + 1e-8 ||
        measures.area < 2200 - 1e-8 || measures.area > 2200 + 1e-8 || measures.solids != 1 ||
        measures.faces != 6 || measures.edges != 12 || measures.generated_count != 6 ||
        measures.history_count != 0)
      return 5;
    nf_native_generated face = {0};
    uint32_t same = 0;
    if (nf_native_result_generated(result, 2, &face) != NF_NATIVE_OK || face.kind != 1 ||
        !face.has_from ||
        nf_native_entities_same(face.entity, face.entity, &same) != NF_NATIVE_OK || same != 1)
      return 6;
    nf_native_result_release(result);
    if (nf_native_result_query(moved, &measures) != NF_NATIVE_OK || measures.history_count != 18 ||
        measures.centroid[0] < 12 - 1e-8 || measures.centroid[0] > 12 + 1e-8)
      return 8;
    for (uint64_t i = 0; i < measures.history_count; ++i) {
      nf_native_history row = {0};
      nf_native_entity_view after = {0};
      if (nf_native_result_history(moved, i, &row) != NF_NATIVE_OK || row.modified_count != 1 ||
          row.generated_count != 0 || row.deleted || !row.before ||
          nf_native_result_history_entity(moved, i, 0, 0, &after) != NF_NATIVE_OK ||
          after.kind != row.before_kind ||
          nf_native_entities_same(row.before, row.before, &same) != NF_NATIVE_OK || same != 1)
        return 9;
    }
    nf_native_result_release(moved);
  } else if (status != NF_NATIVE_UNAVAILABLE || context)
    return 3;
  nf_native_context_release(context);
  nf_native_context_release(NULL);
  return 0;
}
