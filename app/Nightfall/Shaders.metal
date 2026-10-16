// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
// SPIKE (#9): flat-shaded mesh with a headlight.

#include <metal_stdlib>
using namespace metal;

struct VertexIn {
  packed_float3 position;
  packed_float3 normal;
};

struct Uniforms {
  float4x4 modelViewProjection;
  float4x4 modelView;
};

struct VertexOut {
  float4 position [[position]];
  float3 normal;
};

vertex VertexOut mesh_vertex(const device VertexIn* vertices [[buffer(0)]],
                             constant Uniforms& u [[buffer(1)]], uint id [[vertex_id]]) {
  VertexOut out;
  out.position = u.modelViewProjection * float4(float3(vertices[id].position), 1.0);
  out.normal = (u.modelView * float4(float3(vertices[id].normal), 0.0)).xyz;
  return out;
}

fragment float4 mesh_fragment(VertexOut in [[stage_in]]) {
  const float3 n = normalize(in.normal);
  const float light = 0.25 + 0.75 * abs(n.z);  // headlight along the view axis
  return float4(float3(0.62, 0.68, 0.78) * light, 1.0);
}
