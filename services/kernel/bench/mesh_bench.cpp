// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
// Measures display-mesh generation separately from document rebuild and rendering.
#include "evaluator.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>

using namespace nf;
using namespace nf::kernel_service;

int main() {
  try {
    auto backend = kernel::make_occt_kernel().value();
    Evaluator evaluator(*backend, {128, 64 * 1024 * 1024});
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(NF_REFERENCE_PARTS_DIR))
      if (entry.path().extension() == ".nightfall") paths.push_back(entry.path());
    std::ranges::sort(paths);
    for (const auto& path : paths) {
      std::ifstream stream(path);
      const std::string text{std::istreambuf_iterator<char>{stream}, {}};
      const auto built = evaluator.evaluate(text);
      if (!built || !built->ok()) throw std::runtime_error("reference part failed");
      const auto shape = built->bodies.at("part").shape;
      for (const auto& options : {kernel::MeshingOptions{}, kernel::MeshingOptions{0.02, 0.15}}) {
        std::vector<double> times;
        std::size_t vertices = 0, triangles = 0, buffers = 0;
        double volume_error = 0, area_error = 0;
        for (int sample = -1; sample < 30; ++sample) {
          const auto start = std::chrono::steady_clock::now();
          const auto mesh = backend->tessellate(shape, options);
          const auto stop = std::chrono::steady_clock::now();
          if (!mesh) throw std::runtime_error(mesh.error().message);
          if (mesh->faces.size() != shape->measures().faces || mesh->indices.empty())
            throw std::runtime_error("incomplete viewport mesh");
          double volume = 0, area = 0;
          const auto origin = mesh->vertices.front().position;
          for (std::size_t i = 0; i < mesh->indices.size(); i += 3) {
            const auto a = mesh->vertices.at(mesh->indices[i]).position - origin;
            const auto b = mesh->vertices.at(mesh->indices[i + 1]).position - origin;
            const auto c = mesh->vertices.at(mesh->indices[i + 2]).position - origin;
            volume += math::dot(a, math::cross(b, c)) / 6;
            area += math::length(math::cross(b - a, c - a)) / 2;
          }
          volume_error = std::abs(volume / shape->measures().volume - 1);
          area_error = std::abs(area / shape->measures().area - 1);
          if (volume_error > 0.01 || area_error > 0.01)
            throw std::runtime_error(path.stem().string() + ": relative mesh volume/area errors " +
                                     std::to_string(volume_error) + "/" +
                                     std::to_string(area_error) + " exceed 1%");
          if (sample >= 0 &&
              (vertices != mesh->vertices.size() || triangles != mesh->indices.size() / 3))
            throw std::runtime_error("mesh counts changed across identical requests");
          vertices = mesh->vertices.size();
          triangles = mesh->indices.size() / 3;
          buffers = vertices * sizeof(kernel::MeshVertex) +
                    mesh->indices.size() * sizeof(std::uint32_t) +
                    mesh->faces.size() * sizeof(kernel::MeshFace);
          if (sample >= 0)
            times.push_back(std::chrono::duration<double, std::milli>(stop - start).count());
        }
        std::ranges::sort(times);
        std::printf(
            "%-20s %-7s median %.3f ms p95 %.3f ms max %.3f ms vertices %zu triangles %zu "
            "faces %zu buffers %zu bytes volume_error %.8f area_error %.8f\n",
            path.stem().c_str(), options.linear_deflection == 0.1 ? "default" : "fine",
            (times[14] + times[15]) / 2, times[28], times.back(), vertices, triangles,
            shape->measures().faces, buffers, volume_error, area_error);
        std::fflush(stdout);
      }
    }
  } catch (const std::exception& error) {
    std::fprintf(stderr, "mesh benchmark failed: %s\n", error.what());
    return 1;
  }
}
