// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
// Private developer benchmark: real document rebuild, mesh and C-bridge transport.
#include "evaluator.hpp"
#include "kernel_bridge.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>

using namespace nf;
using namespace nf::kernel_service;
using ir::json::Array;
using ir::json::Object;
namespace {
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
void emit(Object object) {
  std::puts(ir::json::canonical(std::move(object)).c_str());
  std::fflush(stdout);
}
void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}
Evaluation build(Evaluator& evaluator, const std::string& source) {
  auto result = evaluator.evaluate(source);
  require(result.has_value(), "document validation failed");
  for (const auto& feature : result->features)
    for (const auto& error : feature.errors)
      std::fprintf(stderr, "%s: %s\n", error.pointer.c_str(), error.message.c_str());
  require(result->ok(), "feature rebuild failed");
  return std::move(*result);
}
}  // namespace
int main(int argc, char** argv) {
  try {
    require(argc == 2 || argc == 3, "usage: nf_showcase_bench part.nightfall [samples=5]");
    const int samples = argc == 3 ? std::stoi(argv[2]) : 5;
    require(samples >= 1 && samples <= 30, "samples must be 1..30");
    std::ifstream file(argv[1]);
    require(file.good(), "cannot read input");
    const std::string source{std::istreambuf_iterator<char>{file}, {}};
    auto backend = kernel::make_occt_kernel().value();
    Evaluator evaluator(*backend, {96, 64 * 1024 * 1024});
    emit({{"kind", "configuration"},
          {"source", argv[1]},
          {"source_bytes", static_cast<double>(source.size())},
          {"kernel", std::string(backend->identity())},
          {"samples", static_cast<double>(samples)}});
    for (int sample = 0; sample < samples; ++sample) {
      evaluator.clear_cache();
      const auto start = Clock::now();
      auto result = build(evaluator, source);
      const double cold = elapsed(start);
      std::size_t bodies = 0, solids = 0, faces = 0, edges = 0;
      std::size_t triangles = 0, vertices = 0, buffer_bytes = 0;
      double volume = 0, area = 0, mesh_volume = 0, mesh_area = 0;
      const auto mesh_start = Clock::now();
      for (const auto& [id, body] : result.bodies) {
        if (!body.shape) continue;
        ++bodies;
        const auto& m = body.shape->measures();
        solids += m.solids;
        faces += m.faces;
        edges += m.edges;
        volume += m.volume;
        area += m.area;
        kernel::MeshingOptions settings;
        require(triangles < settings.max_triangles, "total mesh budget exhausted");
        settings.max_triangles -= triangles;
        const auto mesh = backend->tessellate(body.shape, settings);
        if (!mesh) throw std::runtime_error(id + ": " + mesh.error().message);
        require(mesh->faces.size() == m.faces, "incomplete mesh face coverage");
        const auto origin = mesh->vertices.front().position;
        for (std::size_t i = 0; i < mesh->indices.size(); i += 3) {
          const auto a = mesh->vertices.at(mesh->indices[i]).position - origin;
          const auto b = mesh->vertices.at(mesh->indices[i + 1]).position - origin;
          const auto c = mesh->vertices.at(mesh->indices[i + 2]).position - origin;
          mesh_volume += math::dot(a, math::cross(b, c)) / 6;
          mesh_area += math::length(math::cross(b - a, c - a)) / 2;
        }
        triangles += mesh->indices.size() / 3;
        vertices += mesh->vertices.size();
        buffer_bytes += mesh->vertices.size() * sizeof(kernel::MeshVertex) +
                        mesh->indices.size() * sizeof(std::uint32_t) +
                        mesh->faces.size() * sizeof(nf_face);
      }
      const double meshing = elapsed(mesh_start);
      const auto warm_start = Clock::now();
      const auto warm = build(evaluator, source);
      const double warm_ms = elapsed(warm_start);
      require(std::abs(mesh_volume / volume - 1) < 0.01 && std::abs(mesh_area / area - 1) < 0.01,
              "mesh differs from exact mass by more than 1%");
      const auto usage = evaluator.cache_usage();
      emit({{"kind", "sample"},
            {"sample", static_cast<double>(sample)},
            {"cold_ms", cold},
            {"warm_ms", warm_ms},
            {"mesh_ms", meshing},
            {"bodies", static_cast<double>(bodies)},
            {"solids", static_cast<double>(solids)},
            {"faces", static_cast<double>(faces)},
            {"edges", static_cast<double>(edges)},
            {"triangles", static_cast<double>(triangles)},
            {"vertices", static_cast<double>(vertices)},
            {"buffer_bytes", static_cast<double>(buffer_bytes)},
            {"volume_mm3", volume},
            {"area_mm2", area},
            {"relative_mesh_volume_error", std::abs(mesh_volume / volume - 1)},
            {"relative_mesh_area_error", std::abs(mesh_area / area - 1)},
            {"cold_kernel_calls", static_cast<double>(result.kernel_calls)},
            {"warm_kernel_calls", static_cast<double>(warm.kernel_calls)},
            {"warm_cache_hits", static_cast<double>(warm.cache_hits)},
            {"cache_entries", static_cast<double>(usage.entries)},
            {"cache_estimated_bytes", static_cast<double>(usage.estimated_bytes)}});
    }
    auto session = std::unique_ptr<nf_session, decltype(&nf_session_destroy)>(nf_session_create(),
                                                                              nf_session_destroy);
    require(session != nullptr, "bridge session unavailable");
    for (const auto* mode : {"cold", "warm"}) {
      const auto start = Clock::now();
      auto result = std::unique_ptr<nf_result, decltype(&nf_result_free)>(
          nf_session_build(session.get(), source.data(), source.size(), nullptr), nf_result_free);
      const double ms = elapsed(start);
      require(result != nullptr, "null bridge result");
      std::size_t bytes = 0;
      const auto manifest = ir::json::parse(nf_result_json(result.get(), &bytes)).value();
      require(manifest.find("errors")->if_array()->empty(), "bridge returned errors");
      std::size_t vertices = 0, indices = 0;
      const auto* vertex_data = nf_result_vertices(result.get(), &vertices);
      const auto* index_data = nf_result_indices(result.get(), &indices);
      emit({{"kind", "bridge"},
            {"mode", mode},
            {"ms", ms},
            {"manifest_bytes", static_cast<double>(bytes)},
            {"face_names_sha256",
             ir::sha256(ir::json::canonical(*manifest.find("face_names"))).hex()},
            {"vertices_sha256", ir::sha256({reinterpret_cast<const char*>(vertex_data),
                                            vertices * sizeof(*vertex_data)})
                                    .hex()},
            {"indices_sha256",
             ir::sha256({reinterpret_cast<const char*>(index_data), indices * sizeof(*index_data)})
                 .hex()}});
    }
  } catch (const std::exception& error) {
    std::fprintf(stderr, "showcase benchmark failed: %s\n", error.what());
    return 1;
  }
}
