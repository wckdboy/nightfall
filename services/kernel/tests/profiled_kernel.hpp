// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once
// Private profiling decorator. Only weak ownership keys are retained; geometry is untouched.
#include "nf/ir/json.hpp"
#include "nf/kernel/kernel.hpp"

#include <chrono>
#include <map>

namespace nf::kernel_service::test {
class ProfiledKernel final : public kernel::IKernel {
  kernel::IKernel& real_;
  struct Origin {
    std::size_t id;
    std::string feature;
  };
  std::map<std::weak_ptr<const kernel::Shape>, Origin, std::owner_less<>> origins_;
  ir::json::Object input(const kernel::ShapePtr& shape) {
    if (!shape) return {};
    const auto& m = shape->measures();
    auto [it, inserted] = origins_.try_emplace(shape, Origin{origins_.size(), "unknown"});
    (void)inserted;
    return {{"transient_shape_id", static_cast<double>(it->second.id)},
            {"producer_feature", it->second.feature},
            {"volume_mm3", m.volume},
            {"solids", static_cast<double>(m.solids)},
            {"faces", static_cast<double>(m.faces)}};
  }
  template <class F>
  auto call(const char* name, const kernel::ShapePtr& shape, F action) {
    auto description = input(shape);
    const auto start = std::chrono::steady_clock::now();
    auto result = action();
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    native_ms[feature] += ms;
    ir::json::Object row{{"feature", feature},
                         {"operation", name},
                         {"wall_ms", ms},
                         {"input", std::move(description)},
                         {"ok", result.has_value()}};
    if constexpr (requires {
                    result->shape;
                    result->history;
                  }) {
      if (result) {
        auto [it, inserted] = origins_.try_emplace(result->shape, Origin{origins_.size(), feature});
        (void)it;
        (void)inserted;
        row.push_back({"output", input(result->shape)});
        row.push_back({"history_records", static_cast<double>(result->history.size())});
        row.push_back({"generated_records", static_cast<double>(result->generated.size())});
      }
    }
    calls.push_back(std::move(row));
    return result;
  }

 public:
  explicit ProfiledKernel(kernel::IKernel& real) : real_(real) {}
  std::string feature;
  std::map<std::string, double> native_ms;
  ir::json::Array calls;
  std::string_view identity() const noexcept override { return real_.identity(); }
  Capabilities capabilities() const noexcept override { return real_.capabilities(); }
  std::expected<kernel::OperationResult, kernel::KernelError> extrude(
      const kernel::Extrusion& x) override {
    return call("extrude", {}, [&] { return real_.extrude(x); });
  }
  std::expected<kernel::OperationResult, kernel::KernelError> revolve(
      const kernel::Revolution& x) override {
    return call("revolve", {}, [&] { return real_.revolve(x); });
  }
  std::expected<kernel::OperationResult, kernel::KernelError> loft(const kernel::Loft& x) override {
    return call("loft", {}, [&] { return real_.loft(x); });
  }
  std::expected<kernel::OperationResult, kernel::KernelError> sweep(
      const kernel::Sweep& x) override {
    return call("sweep", {}, [&] { return real_.sweep(x); });
  }
  std::expected<kernel::OperationResult, kernel::KernelError> transform(
      const kernel::ShapePtr& s, const kernel::RigidTransform& x) override {
    return call("transform", s, [&] { return real_.transform(s, x); });
  }
  std::expected<kernel::OperationResult, kernel::KernelError> mirror(
      const kernel::ShapePtr& s, const kernel::Plane& x) override {
    return call("mirror", s, [&] { return real_.mirror(s, x); });
  }
  std::expected<kernel::OperationResult, kernel::KernelError> boolean_operation(
      const kernel::ShapePtr& s, const std::vector<kernel::ShapePtr>& t,
      kernel::BooleanOp x) override {
    return call("boolean", s, [&] { return real_.boolean_operation(s, t, x); });
  }
  std::expected<kernel::OperationResult, kernel::KernelError> chamfer(
      const kernel::ShapePtr& s, const std::vector<kernel::EntityPtr>& e, double d) override {
    return call("chamfer", s, [&] { return real_.chamfer(s, e, d); });
  }
  std::expected<kernel::OperationResult, kernel::KernelError> fillet(
      const kernel::ShapePtr& s, const std::vector<kernel::EntityPtr>& e, double d) override {
    return call("fillet", s, [&] { return real_.fillet(s, e, d); });
  }
  std::expected<std::vector<kernel::FaceInfo>, kernel::KernelError> faces(
      const kernel::ShapePtr& s) override {
    return call("faces", s, [&] { return real_.faces(s); });
  }
  std::expected<std::vector<kernel::EntityPtr>, kernel::KernelError> edges(
      const kernel::EntityPtr& e) override {
    return call("edges", e ? e->owner() : nullptr, [&] { return real_.edges(e); });
  }
  std::expected<kernel::Plane, kernel::KernelError> face_plane(
      const kernel::EntityPtr& e) override {
    return call("face_plane", e ? e->owner() : nullptr, [&] { return real_.face_plane(e); });
  }
  std::expected<double, kernel::KernelError> face_distance(const kernel::EntityPtr& e,
                                                           math::Vec3 p) override {
    return call("face_distance", e ? e->owner() : nullptr,
                [&] { return real_.face_distance(e, p); });
  }
  std::expected<kernel::Mesh, kernel::KernelError> tessellate(
      const kernel::ShapePtr& s, const kernel::MeshingOptions& o) override {
    return call("tessellate", s, [&] { return real_.tessellate(s, o); });
  }
  std::expected<std::string, kernel::KernelError> export_step(
      const std::vector<kernel::ShapePtr>& s) override {
    return call("step", {}, [&] { return real_.export_step(s); });
  }
};
}  // namespace nf::kernel_service::test
