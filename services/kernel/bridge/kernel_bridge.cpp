// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "kernel_bridge.h"
#include "assembly.hpp"
#include "evaluator.hpp"

#include <algorithm>
#include <atomic>
#include <map>
#include <unordered_map>

using namespace nf;
using namespace nf::kernel_service;
using ir::json::Array;
using ir::json::Object;
using ir::json::Value;

struct nf_session {
  std::unique_ptr<kernel::IKernel> kernel;
  Evaluator evaluator;
  std::atomic<std::int64_t> active{NF_NO_ACTIVE_FEATURE};
  explicit nf_session(std::unique_ptr<kernel::IKernel> k)
      : kernel(std::move(k)), evaluator(*kernel, {96, 64 * 1024 * 1024}) {}
};
struct nf_result {
  std::string json;
  std::vector<nf_vertex> vertices;
  std::vector<std::uint32_t> indices;
  std::vector<nf_face> faces;
};
namespace {
Value numeric(std::size_t n) { return static_cast<double>(n); }
Value vector(math::Vec3 p) { return Array{p.x, p.y, p.z}; }
Value diagnostic(const ir::Error& e) {
  Object value{{"code", e.code}, {"message", e.message}, {"pointer", e.pointer}};
  if (e.offset) value.push_back({"offset", numeric(*e.offset)});
  return value;
}
[[noreturn]] void fail(std::string code, std::string message) {
  throw ir::Errors{{std::move(code), std::move(message), "", {}}};
}
nf_result* failure(const ir::Errors& errors) {
  auto result = std::make_unique<nf_result>();
  Array values;
  for (const auto& error : errors) values.push_back(diagnostic(error));
  result->json = ir::json::canonical(Object{{"errors", std::move(values)},
                                            {"features", Array{}},
                                            {"bodies", Array{}},
                                            {"face_names", Array{}},
                                            {"cache", Object{}}});
  if (result->json.size() > ir::kMaxDocumentBytes)
    return failure({{"bounds", "diagnostic manifest exceeds 16 MiB", "", {}}});
  return result.release();
}
nf_result* build(nf_session& s, std::string_view source, const nf_mesh_options* options) {
  kernel::MeshingOptions settings;
  if (options)
    settings = {options->linear_deflection, options->angular_deflection, options->max_triangles};
  if (settings.max_triangles == 0 || settings.max_triangles > 1'000'000)
    fail("bounds", "mesh budget must be 1..1,000,000 triangles");
  auto evaluated = s.evaluator.evaluate(source, [&](std::size_t index) {
    s.active.store(static_cast<std::int64_t>(index), std::memory_order_relaxed);
  });
  s.active.store(NF_NO_ACTIVE_FEATURE, std::memory_order_relaxed);
  if (!evaluated) throw evaluated.error();
  auto result = std::make_unique<nf_result>();
  Array features, errors, bodies, face_names;
  constexpr const char* statuses[]{"built", "cached", "suppressed", "rolled_back", "failed"};
  for (std::size_t i = 0; i < evaluated->features.size(); ++i) {
    const auto& f = evaluated->features[i];
    Object item{{"id", f.id}, {"status", statuses[static_cast<std::size_t>(f.status)]}};
    if (f.hash) item.push_back({"hash", f.hash->hex()});
    features.emplace_back(std::move(item));
    for (const auto& error : f.errors) {
      auto value = *diagnostic(error).if_object();
      value.push_back({"feature_index", numeric(i)});
      errors.emplace_back(std::move(value));
    }
  }
  // RFC 0013: an assembly's geometry is its occurrences' bodies at their solved placements,
  // named "occurrence/body"; the root's own bodies are not separate results. A joint conflict
  // fails the build with its pointer and no geometry, like a failed feature.
  const auto* live = &evaluated->bodies;
  std::map<std::string, BodyState> placed;
  if (errors.empty()) {
    auto document = ir::read_document(source);
    if (!document) throw document.error();
    if (!document->occurrences.empty()) {
      const auto assembly = evaluate_assembly(s.evaluator, *s.kernel, *document);
      if (!assembly) {
        for (const auto& error : assembly.error()) errors.emplace_back(diagnostic(error));
      } else {
        for (const auto& o : assembly->occurrences)
          for (const auto& [id, shape] : o.bodies) {
            Array placement;
            for (const double r : o.placement.rotation) placement.push_back(r);
            for (const double t :
                 {o.placement.translation.x, o.placement.translation.y, o.placement.translation.z})
              placement.push_back(t);
            BodyState body;
            body.shape = shape;
            body.hash = ir::sha256(ir::json::canonical(Object{{"occurrence", o.id},
                                                              {"body", id},
                                                              {"placement", std::move(placement)},
                                                              {"volume", shape->measures().volume},
                                                              {"area", shape->measures().area}}));
            placed.emplace(o.id + "/" + id, std::move(body));
          }
      }
      live = &placed;
    }
  }
  if (errors.empty()) {
    for (const auto& [id, body] : *live) {
      if (!body.shape) continue;  // consumed/suppressed bodies have no live geometry
      const auto remaining = settings.max_triangles - result->indices.size() / 3;
      if (remaining == 0) fail("bounds", "total mesh budget exceeded");
      auto limited = settings;
      limited.max_triangles = remaining;
      auto mesh = s.kernel->tessellate(body.shape, limited);
      if (!mesh) fail(mesh.error().code, id + ": " + mesh.error().message);
      const auto base_vertex = static_cast<std::uint32_t>(result->vertices.size());
      const auto base_index = static_cast<std::uint32_t>(result->indices.size());
      struct NameGroup {
        const kernel::Entity* entity;
        std::vector<std::size_t> names;
      };
      std::vector<NameGroup> groups;
      std::unordered_map<const kernel::Entity*, std::size_t> group_indices;
      for (std::size_t i = 0; i < body.names.size(); ++i) {
        const auto* entity = body.names[i].entity.get();
        auto [group, inserted] = group_indices.try_emplace(entity, groups.size());
        if (inserted) groups.push_back({entity, {}});
        groups[group->second].names.push_back(i);
      }
      for (const auto& v : mesh->vertices)
        result->vertices.push_back(
            {{v.position.x, v.position.y, v.position.z}, {v.normal.x, v.normal.y, v.normal.z}});
      for (auto index : mesh->indices) result->indices.push_back(base_vertex + index);
      for (const auto& face : mesh->faces) {
        result->faces.push_back({static_cast<std::uint32_t>(bodies.size()),
                                 base_index + face.first_index, face.index_count});
        std::vector<std::size_t> matched;
        for (const auto& group : groups)
          if (group.entity->same_as(*face.face))
            matched.insert(matched.end(), group.names.begin(), group.names.end());
        // Distinct handles can name the same topology. Keep every alias in its original order.
        std::ranges::sort(matched);
        Array names;
        for (const auto index : matched) {
          const auto& name = body.names[index];
          Object value{{"feature", name.feature}, {"role", name.role}};
          if (!name.from.empty()) value.push_back({"from", name.from});
          names.emplace_back(std::move(value));
        }
        face_names.emplace_back(std::move(names));
      }
      const auto& m = body.shape->measures();
      bodies.emplace_back(Object{{"id", id},
                                 {"hash", body.hash->hex()},
                                 {"volume", m.volume},
                                 {"area", m.area},
                                 {"centroid", vector(m.centroid)},
                                 {"minimum", vector(m.minimum)},
                                 {"maximum", vector(m.maximum)},
                                 {"solids", numeric(m.solids)},
                                 {"faces", numeric(m.faces)},
                                 {"edges", numeric(m.edges)}});
    }
  }
  const auto usage = s.evaluator.cache_usage();
  result->json = ir::json::canonical(
      Object{{"errors", std::move(errors)},
             {"features", std::move(features)},
             {"bodies", std::move(bodies)},
             {"face_names", std::move(face_names)},
             {"cache", Object{{"hits", numeric(evaluated->cache_hits)},
                              {"kernel_calls", numeric(evaluated->kernel_calls)},
                              {"binding_misses", numeric(evaluated->binding_misses)},
                              {"entries", numeric(usage.entries)},
                              {"estimated_bytes", numeric(usage.estimated_bytes)}}}});
  if (result->json.size() > ir::kMaxDocumentBytes) fail("bounds", "result manifest exceeds 16 MiB");
  return result.release();
}
template <class T>
const T* view(const std::vector<T>& data, std::size_t* count) {
  if (count) *count = data.size();
  return data.empty() ? nullptr : data.data();
}
}  // namespace

nf_session* nf_session_create(void) {
  try {
    auto kernel = kernel::make_occt_kernel();
    return kernel ? new nf_session(std::move(*kernel)) : nullptr;
  } catch (...) {
    return nullptr;
  }
}
void nf_session_destroy(nf_session* s) { delete s; }
nf_result* nf_session_build(nf_session* s, const char* source, std::size_t bytes,
                            const nf_mesh_options* options) {
  // Reset on every exit, including validation failures and unexpected native exceptions.
  struct Reset {
    nf_session* s;
    ~Reset() {
      if (s) s->active.store(NF_NO_ACTIVE_FEATURE, std::memory_order_relaxed);
    }
  } reset{s};
  try {
    if (!s) fail("kernel", "session is null");
    if (bytes > ir::kMaxDocumentBytes) fail("bounds", "document exceeds 16 MiB");
    if (!source && bytes != 0) fail("parse", "document pointer is null");
    return build(*s, source ? std::string_view(source, bytes) : std::string_view{}, options);
  } catch (...) {
    try {
      try {
        throw;
      } catch (const ir::Errors& errors) {
        return failure(errors);
      } catch (...) {
        return failure({{"kernel", "unexpected native bridge failure", "", {}}});
      }
    } catch (...) {
      return nullptr;
    }  // even diagnostic allocation may fail
  }
}
std::int64_t nf_session_active_feature(const nf_session* s) {
  return s ? s->active.load(std::memory_order_relaxed) : NF_NO_ACTIVE_FEATURE;
}
const char* nf_result_json(const nf_result* r, std::size_t* bytes) {
  if (bytes) *bytes = r ? r->json.size() : 0;
  return r ? r->json.c_str() : nullptr;
}
const nf_vertex* nf_result_vertices(const nf_result* r, std::size_t* count) {
  if (count) *count = 0;
  return r ? view(r->vertices, count) : nullptr;
}
const std::uint32_t* nf_result_indices(const nf_result* r, std::size_t* count) {
  if (count) *count = 0;
  return r ? view(r->indices, count) : nullptr;
}
const nf_face* nf_result_faces(const nf_result* r, std::size_t* count) {
  if (count) *count = 0;
  return r ? view(r->faces, count) : nullptr;
}
void nf_result_free(nf_result* r) { delete r; }
