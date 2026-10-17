// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "evaluator.hpp"

#include <algorithm>
#include <set>

#include "geometry_cache.hpp"
#include "model_inputs.hpp"
#include "nf/ir/inputs.hpp"
#include "nf/ir/parameters.hpp"

namespace nf::kernel_service {
namespace {
using ir::json::Value;
struct Output {
  std::optional<Sketch> sketch;
  std::optional<kernel::OperationResult> tool;  // private sweep before a result-mode boolean
  std::vector<kernel::OperationResult> operations;
  std::vector<kernel::ShapePtr> bindings;
  std::size_t bytes = sizeof(Output);
};
using Cache = GeometryCache<Output>;
using Targets = std::map<std::string, std::vector<std::string>>;
[[noreturn]] void fail(std::string code, std::string message, std::string pointer) {
  throw ir::Errors{{std::move(code), std::move(message), std::move(pointer), {}}};
}
template <class T, class E>
T take(std::expected<T, E> value) {
  if (!value) {
    if constexpr (std::is_same_v<E, ir::Errors>)
      throw value.error();
    else if constexpr (std::is_same_v<E, ir::Error>)
      throw ir::Errors{value.error()};
    else
      fail(value.error().code, value.error().message, "/inputs");
  }
  return std::move(*value);
}
const std::string& string(const Value& value) { return *value.if_string(); }
kernel::BooleanOp boolean_kind(std::string_view mode) {
  if (mode == "union" || mode == "join") return kernel::BooleanOp::kUnion;
  if (mode == "subtract" || mode == "cut") return kernel::BooleanOp::kSubtract;
  return kernel::BooleanOp::kIntersect;  // schema-checked op/result modes
}
void selector_targets(const Value& value, const Targets& previous, std::set<std::string>& ids) {
  if (const auto* array = value.if_array()) {
    for (const auto& item : *array) selector_targets(item, previous, ids);
  } else if (const auto* object = value.if_object()) {
    for (const auto& member : *object) {
      if (member.key.starts_with("x-")) continue;
      if (member.key == "faces_of")
        ids.insert(string(member.value));
      else if (member.key == "created_by") {
        if (const auto found = previous.find(string(member.value)); found != previous.end())
          ids.insert(found->second.begin(), found->second.end());
      } else
        selector_targets(member.value, previous, ids);
    }
  }
}
std::vector<std::string> targets(const ir::Feature& feature, const Targets& previous) {
  const auto& inputs = feature.inputs;
  if (const auto* result = inputs.find("result")) {
    for (const auto* key : {"new", "join", "cut", "intersect"})
      if (const auto* id = result->find(key)) return {string(*id)};
  }
  if (feature.type == "sketch") return {};
  if (const auto* body = inputs.find("body")) return {string(*body)};
  if (const auto* target = inputs.find("target")) return {string(*target)};
  if (const auto* bodies = inputs.find("bodies")) {
    std::vector<std::string> result;
    for (const auto& id : *bodies->if_array()) result.push_back(string(id));
    return result;
  }
  std::set<std::string> ids;
  if (const auto* features = inputs.find("features"))
    for (const auto& id : *features->if_array()) {
      const auto found = previous.find(string(id));
      if (found != previous.end()) ids.insert(found->second.begin(), found->second.end());
    }
  selector_targets(inputs, previous, ids);
  return {ids.begin(), ids.end()};
}
std::vector<NamedEntity> names(const std::vector<const BodyState*>& inputs,
                               const kernel::OperationResult& operation,
                               const std::string& feature) {
  std::vector<NamedEntity> result;
  auto append = [&](const std::string& producer, const std::string& role,
                    const kernel::EntityPtr& entity) {
    if (!entity || entity->owner() != operation.shape)
      fail("kernel", "naming history belongs to a different snapshot", "/inputs");
    if (std::ranges::none_of(result, [&](const auto& name) {
          return name.feature == producer && name.role == role && name.entity->same_as(*entity);
        }))
      result.push_back({producer, role, entity});
  };
  for (const auto* input : inputs) {
    for (const auto& name : input->names) {
      const auto found = std::ranges::find_if(operation.history, [&](const auto& h) {
        return h.before && h.before->same_as(*name.entity);
      });
      if (found == operation.history.end()) fail("kernel", "incomplete naming history", "/inputs");
      if (!found->deleted && found->modified.empty())
        fail("kernel", "history omitted a surviving entity", "/inputs");
      if (!found->deleted)
        for (const auto& entity : found->modified) append(name.feature, name.role, entity);
    }
  }
  for (const auto& generated : operation.generated)
    append(feature, generated.role, generated.entity);
  return result;
}
std::size_t sketch_bytes(const Sketch& sketch) {
  std::size_t bytes = sizeof(Sketch);
  for (const auto& path : sketch.paths) {
    bytes += sizeof(path);
    for (const auto& curve : path)
      bytes += sizeof(curve) + 128 + curve.points().size() * sizeof(math::Vec2) +
               (curve.weights().size() + curve.knots().size()) * sizeof(double);
  }
  return bytes;
}
}  // namespace

struct Evaluator::State {
  kernel::IKernel& kernel;
  Cache cache;
  State(kernel::IKernel& backend, Limits limits)
      : kernel(backend), cache({limits.entries, limits.estimated_bytes}) {}
};
Evaluator::Evaluator(kernel::IKernel& kernel, Limits limits)
    : state_(std::make_unique<State>(kernel, limits)) {}
Evaluator::~Evaluator() = default;
void Evaluator::set_cache_limits(Limits limits) {
  state_->cache.set_limits({limits.entries, limits.estimated_bytes});
}
void Evaluator::clear_cache() { state_->cache.clear(); }
Evaluator::Usage Evaluator::cache_usage() const {
  const auto stats = state_->cache.stats();
  return {stats.entries, stats.estimated_bytes};
}
bool Evaluation::ok() const {
  return std::ranges::none_of(features,
                              [](const auto& f) { return f.status == FeatureStatus::kFailed; });
}

std::expected<Evaluation, ir::Errors> Evaluator::evaluate(std::string_view source) {
  auto document = ir::read_document(source);
  if (!document) return std::unexpected(document.error());
  auto parameters = ir::evaluate_parameters(document->params);
  if (!parameters) return std::unexpected(parameters.error());
  const auto version = "evaluator/6;roles/3;" + std::string(state_->kernel.identity());
  Evaluation result;
  result.features.reserve(document->features.size());
  Targets previous;
  std::map<std::string, std::size_t> features;
  std::map<std::string, std::shared_ptr<const Output>> sketches;
  bool rolled_back = false;
  for (std::size_t index = 0; index < document->features.size(); ++index) {
    const auto& feature = document->features[index];
    const bool profile_feature = feature.type == "extrude" || feature.type == "revolve";
    result.features.push_back({feature.id, FeatureStatus::kFailed, {}, {}});
    auto& record = result.features.back();
    const auto affected = targets(feature, previous);
    previous.emplace(feature.id, affected);
    features.emplace(feature.id, index);
    const bool skip = rolled_back;
    if (document->history_end == feature.id) rolled_back = true;
    if (skip) {
      record.status = FeatureStatus::kRolledBack;
      continue;
    }
    if (feature.suppressed.value_or(false)) {
      record.status = FeatureStatus::kSuppressed;
      if (const auto* output = feature.inputs.find("result"); output && output->find("new"))
        result.bodies[string(*output->find("new"))].unavailable = "input_suppressed";
      continue;
    }
    try {
      const auto program = take(ir::FeatureInputs::compile(feature));
      const auto inputs = take(program.evaluate(document->units, parameters->values));
      std::map<std::string, ir::Hash> dependencies;  // RFC 6901 lexicographic pointer order
      std::vector<kernel::ShapePtr> bindings;
      std::vector<std::string> tools;
      auto bind = [&](const std::string& id, const std::string& pointer) {
        const auto& body = result.bodies.at(id);
        if (!body.shape || !body.hash)
          fail(body.unavailable, "body is unavailable: " + id, pointer);
        dependencies.emplace(pointer, *body.hash);
        bindings.push_back(body.shape);
      };
      std::shared_ptr<const Output> sketch;
      std::string result_mode;
      if (profile_feature) {
        const auto& id = string(*inputs.find("profile")->find("sketch"));
        const auto& upstream = result.features[features.at(id)];
        if (!upstream.hash)
          fail(upstream.status == FeatureStatus::kSuppressed ? "input_suppressed" : "input_failed",
               "profile sketch is unavailable: " + id, "/inputs/profile/sketch");
        dependencies.emplace("/inputs/profile/sketch", *upstream.hash);
        sketch = sketches.at(id);
        for (const auto* mode : {"join", "cut", "intersect"}) {
          if (!inputs.find("result")->find(mode)) continue;
          result_mode = mode;
          bind(affected.front(), "/inputs/result/" + result_mode);
        }
      } else if (feature.type == "transform") {
        for (std::size_t i = 0; i < affected.size(); ++i)
          bind(affected[i], "/inputs/bodies/" + std::to_string(i));
      } else if (feature.type == "boolean") {
        bind(affected.front(), "/inputs/target");
        for (const auto& tool : *inputs.find("tools")->if_array()) {
          const auto& id = string(tool);
          const auto pointer = "/inputs/tools/" + std::to_string(tools.size());
          if (id == affected.front()) fail("kernel", "target cannot also be a tool", pointer);
          bind(id, pointer);
          tools.push_back(id);
        }
      } else if (feature.type != "sketch") {
        fail("kernel", "feature is not implemented: " + feature.type, "/type");
      }
      std::vector<ir::Hash> upstream;
      for (const auto& [pointer, hash] : dependencies) upstream.push_back(hash);
      const auto key = ir::feature_hash(version, feature.type, inputs, upstream);
      auto output = state_->cache.find(key);
      if (output && output->bindings != bindings) {
        output.reset();
        ++result.binding_misses;
      }
      const bool cached = static_cast<bool>(output);
      if (!output) {
        auto built = std::make_shared<Output>();
        built->bindings = bindings;
        built->bytes += bindings.size() * sizeof(kernel::ShapePtr);
        if (feature.type == "sketch") {
          built->sketch = take(prepare_sketch(inputs));
          built->bytes += sketch_bytes(*built->sketch);
        } else if (profile_feature) {
          auto solid = [&] {
            if (feature.type == "extrude") {
              const auto operation = take(prepare_extrusion(inputs, *sketch->sketch));
              ++result.kernel_calls;
              return take(state_->kernel.extrude(operation));
            }
            const auto operation = take(prepare_revolution(inputs, *sketch->sketch));
            ++result.kernel_calls;
            return take(state_->kernel.revolve(operation));
          }();
          if (result_mode.empty()) {
            built->operations.push_back(std::move(solid));
          } else {
            built->tool = std::move(solid);
            built->bytes += built->tool->estimated_bytes;
            ++result.kernel_calls;
            built->operations.push_back(take(state_->kernel.boolean_operation(
                bindings.front(), {built->tool->shape}, boolean_kind(result_mode))));
          }
        } else if (feature.type == "boolean") {
          const auto& op = string(*inputs.find("op"));
          ++result.kernel_calls;
          built->operations.push_back(take(state_->kernel.boolean_operation(
              bindings.front(), {bindings.begin() + 1, bindings.end()}, boolean_kind(op))));
        } else {
          const auto operation = take(prepare_transform(inputs));
          for (const auto& input : bindings) {
            ++result.kernel_calls;
            built->operations.push_back(take(state_->kernel.transform(input, operation)));
          }
        }
        for (const auto& operation : built->operations) built->bytes += operation.estimated_bytes;
        output = std::move(built);
      }
      std::vector<BodyState> published;
      for (std::size_t i = 0; i < output->operations.size(); ++i) {
        const auto& operation = output->operations[i];
        if (!operation.shape) fail("kernel", "backend returned no shape", "/inputs");
        const auto* input = bindings.empty() ? nullptr : &result.bodies.at(affected[i]);
        std::vector<const BodyState*> named_inputs;
        if (input) named_inputs.push_back(input);
        for (const auto& tool : tools) named_inputs.push_back(&result.bodies.at(tool));
        BodyState temporary;
        if (output->tool) {
          temporary.names = names({}, *output->tool, feature.id);
          named_inputs.push_back(&temporary);
        }
        published.push_back({operation.shape,
                             ir::body_hash(input ? input->hash : std::nullopt, key),
                             names(named_inputs, operation, feature.id),
                             {}});
      }
      // Publish complete geometry and naming together, after every operation/history succeeds.
      for (std::size_t i = 0; i < published.size(); ++i)
        result.bodies[affected[i]] = std::move(published[i]);
      const auto* keep = inputs.find("keep_tools");
      if (!keep || !*keep->if_bool())
        for (const auto& tool : tools) result.bodies[tool] = {{}, {}, {}, "input_failed"};
      if (output->sketch) sketches[feature.id] = output;
      if (cached)
        ++result.cache_hits;
      else
        (void)state_->cache.insert(key, output, output->bytes);
      record.hash = key;
      record.status = cached ? FeatureStatus::kCached : FeatureStatus::kBuilt;
    } catch (ir::Errors& errors) {
      for (auto& error : errors)
        error.pointer = "/features/" + std::to_string(index) + error.pointer;
      record.errors = std::move(errors);
      for (const auto& id : affected) result.bodies[id] = {{}, {}, {}, "input_failed"};
    }
  }
  return result;
}
}  // namespace nf::kernel_service
