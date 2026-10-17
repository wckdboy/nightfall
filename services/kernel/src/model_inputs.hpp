// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "nf/ir/document.hpp"
#include "nf/kernel/kernel.hpp"

namespace nf::kernel_service {
struct Sketch {
  kernel::Plane plane;
  std::vector<std::vector<math::NurbsCurve2>> paths;
};
/// Private service adapters: accept only schema-checked, dimensionally evaluated FeatureInputs.
/// Preserve exact curves; errors have feature-relative pointers, prefixed by the evaluator.
/// Constraints and unimplemented entity/selector/mode semantics fail explicitly.
std::expected<Sketch, ir::Error> prepare_sketch(const ir::json::Value& inputs);
std::expected<kernel::Extrusion, ir::Error> prepare_extrusion(const ir::json::Value& inputs,
                                                              const Sketch& sketch);
std::expected<kernel::RigidTransform, ir::Error> prepare_transform(const ir::json::Value& inputs);
}  // namespace nf::kernel_service
