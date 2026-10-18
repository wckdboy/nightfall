// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "kernel_bridge.h"

#include <string.h>

_Static_assert(sizeof(nf_vertex) == 6 * sizeof(double), "vertex layout");
_Static_assert(sizeof(nf_face) == 3 * sizeof(uint32_t), "face layout");
int main(void) {
  nf_session* session = nf_session_create();
  if (!session) return 1;
  nf_result* result = nf_session_build(session, "{}", 2, NULL);
  nf_session_destroy(session);
  if (!result) return 2;
  size_t bytes = 0, count = 123;
  const char* json = nf_result_json(result, &bytes);
  int status = !json || bytes == 0 || !strstr(json, "\"errors\"");
  status |= nf_result_vertices(result, &count) != NULL || count != 0;
  status |= nf_result_indices(result, &count) != NULL || count != 0;
  status |= nf_result_faces(result, &count) != NULL || count != 0;
  nf_result_free(result);
  static const char part[] =
      "{\"format\":\"nightfall.part\",\"version\":1,\"units\":{\"length\":\"mm\",\"angle\":\"deg\"}"
      ","
      "\"meta\":{},\"params\":[],\"references\":[],\"features\":[{\"id\":\"outline\",\"type\":"
      "\"sketch\","
      "\"inputs\":{\"plane\":{\"datum\":\"xy\"},\"entities\":[{\"id\":\"r\",\"type\":\"rect\","
      "\"corner\":[0,0],\"size\":[10,20]}],\"constraints\":[]}},{\"id\":\"base\",\"type\":"
      "\"extrude\","
      "\"inputs\":{\"profile\":{\"sketch\":\"outline\"},\"distance\":30,"
      "\"result\":{\"new\":\"plate\",\"body_type\":\"brep\"}}}],\"history_end\":null,\"ui\":{}}";
  session = nf_session_create();
  if (!session) return 4;
  nf_mesh_options options = {0.1, 0.2, 12};
  result = nf_session_build(session, part, sizeof(part) - 1, &options);
  nf_session_destroy(session);
  if (!result) return 5;
  json = nf_result_json(result, NULL);
  status |= !json || !strstr(json, "\"errors\":[]");
  const nf_vertex* vertices = nf_result_vertices(result, &count);
  status |= !vertices || count != 24;
  const uint32_t* indices = nf_result_indices(result, &count);
  status |= !indices || count != 36;
  const nf_face* faces = nf_result_faces(result, &count);
  status |= !faces || count != 6;
  if (faces) status |= faces[0].first_index != 0 || faces[0].body_index != 0;
  nf_result_free(result);
  nf_result_free(NULL);
  nf_session_destroy(NULL);
  return status ? 3 : 0;
}
