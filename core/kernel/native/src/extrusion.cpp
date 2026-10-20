// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include <limits>
#include <span>
#include "context.hpp"
#include "guard.hpp"
#if NF_NATIVE_OCCT
struct nf_native_result {
  std::shared_ptr<nf::kernel::IKernel> backend;
  nf::kernel::OperationResult operation;
};
namespace {
struct Invalid {};
template <class T>
std::span<const T> range(const T* pointer, size_t count) {
  if (count > static_cast<size_t>(PTRDIFF_MAX) / sizeof(T) ||
      (count && (!pointer || reinterpret_cast<uintptr_t>(pointer) % alignof(T))))
    throw Invalid{};
  return count ? std::span<const T>{pointer, count} : std::span<const T>{};
}
template <class T>
std::vector<T> copied(const T* pointer, size_t count) {
  const auto input = range(pointer, count);
  return input.empty() ? std::vector<T>{} : std::vector<T>{input.begin(), input.end()};
}
nf::math::Loop decode(const nf_native_loop2& input) {
  nf::math::Loop loop;
  if (!input.count) throw Invalid{};
  for (const auto& c : range(input.curves, input.count)) {
    // Existing basis/native knot indices add two signed ints during span search.
    if (c.degree < 1 || c.degree > 9 || c.knot_count < 2 * (static_cast<size_t>(c.degree) + 1) ||
        c.knot_count > static_cast<size_t>(std::numeric_limits<int>::max()) / 2 ||
        c.point_count != c.knot_count - c.degree - 1 ||
        (c.weight_count && c.weight_count != c.point_count))
      throw Invalid{};
    auto knots =
        nf::math::KnotVector::make(copied(c.knots, c.knot_count), static_cast<int>(c.degree));
    if (!knots) throw Invalid{};
    std::vector<nf::math::Vec2> points;
    for (auto p : range(c.points, c.point_count)) points.push_back({p.x, p.y});
    auto curve =
        nf::math::NurbsCurve2::make(*knots, std::move(points), copied(c.weights, c.weight_count));
    if (!curve) throw Invalid{};
    loop.curves.push_back(std::move(*curve));
  }
  return loop;
}
nf::kernel::Extrusion decode(const nf_native_extrusion* pointer) {
  const auto& input = range(pointer, 1).front();
  const auto vec = [](const double* p) { return nf::math::Vec3{p[0], p[1], p[2]}; };
  nf::kernel::Extrusion result{
      {vec(input.origin), vec(input.x_axis), vec(input.normal)}, {}, vec(input.displacement)};
  if (!input.region_count) throw Invalid{};
  for (const auto& region : range(input.regions, input.region_count)) {
    nf::math::Region r{decode(region.outer), {}};
    for (const auto& hole : range(region.holes, region.hole_count)) r.holes.push_back(decode(hole));
    result.regions.push_back(std::move(r));
  }
  return result;
}
}  // namespace
#else
struct nf_native_result {};
#endif
uint32_t nf_native_extrude(nf_native_context* context, const nf_native_extrusion* input,
                           nf_native_result** out) noexcept {
  if (out) *out = nullptr;
  if (!context || !input || !out) return NF_NATIVE_INVALID;
  return nf::native::guarded([&]() -> uint32_t {
#if NF_NATIVE_OCCT
    try {
      auto result = context->kernel->extrude(decode(input));
      if (!result) return NF_NATIVE_FAILURE;
      *out = new nf_native_result{context->kernel, std::move(*result)};
      return NF_NATIVE_OK;
    } catch (const Invalid&) {
      return NF_NATIVE_INVALID;
    }
#else
    return NF_NATIVE_UNAVAILABLE;
#endif
  });
}
uint32_t nf_native_result_query(const nf_native_result* result,
                                nf_native_result_info* out) noexcept {
  if (out) *out = {};
  if (!result || !out) return NF_NATIVE_INVALID;
  return nf::native::guarded([&]() -> uint32_t {
#if NF_NATIVE_OCCT
    static_assert(sizeof(size_t) <= sizeof(uint64_t));
    const auto& op = result->operation;
    const auto& m = op.shape->measures();
    *out = {m.volume,
            m.area,
            {m.centroid.x, m.centroid.y, m.centroid.z},
            {m.minimum.x, m.minimum.y, m.minimum.z},
            {m.maximum.x, m.maximum.y, m.maximum.z},
            m.solids,
            m.faces,
            m.edges,
            op.estimated_bytes,
            op.generated.size(),
            op.history.size()};
    return NF_NATIVE_OK;
#else
    return NF_NATIVE_UNAVAILABLE;
#endif
  });
}
uint32_t nf_native_result_generated(const nf_native_result* result, [[maybe_unused]] uint64_t index,
                                    nf_native_generated* out) noexcept {
  if (out) *out = {};
  if (!result || !out) return NF_NATIVE_INVALID;
  return nf::native::guarded([&]() -> uint32_t {
#if NF_NATIVE_OCCT
    if (index >= result->operation.generated.size()) return NF_NATIVE_INVALID;
    const auto& g = result->operation.generated[static_cast<size_t>(index)];
    *out = {reinterpret_cast<const uint8_t*>(g.role.data()),
            g.role.size(),
            reinterpret_cast<const nf_native_entity*>(g.entity.get()),
            0,
            0,
            0,
            0,
            static_cast<uint32_t>(g.from.has_value()),
            static_cast<uint32_t>(g.entity->kind()) + 1u};
    if (g.from) {
      out->region = g.from->region;
      out->loop = g.from->loop;
      out->curve = g.from->curve;
      out->section = g.from->section;
    }
    return NF_NATIVE_OK;
#else
    return NF_NATIVE_UNAVAILABLE;
#endif
  });
}
uint32_t nf_native_entities_same(const nf_native_entity* a, const nf_native_entity* b,
                                 uint32_t* out) noexcept {
  if (out) *out = 0;
  if (!a || !b || !out) return NF_NATIVE_INVALID;
  return nf::native::guarded([&]() -> uint32_t {
#if NF_NATIVE_OCCT
    *out = static_cast<uint32_t>(reinterpret_cast<const nf::kernel::Entity*>(a)->same_as(
        *reinterpret_cast<const nf::kernel::Entity*>(b)));
    return NF_NATIVE_OK;
#else
    return NF_NATIVE_UNAVAILABLE;
#endif
  });
}
void nf_native_result_release(nf_native_result* result) noexcept {
  (void)nf::native::guarded([&]() -> uint32_t {
    delete result;
    return NF_NATIVE_OK;
  });
}
