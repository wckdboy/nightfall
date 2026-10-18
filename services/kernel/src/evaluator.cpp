// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "evaluator.hpp"

#include <algorithm>
#include <functional>
#include <limits>
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
  std::vector<std::vector<kernel::OperationResult>> copies;  // private per-body pattern history
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
struct PlaneFilter {
  bool planar = false;
  std::optional<math::Vec3> normal, parallel, perpendicular;
  std::optional<math::Vec3> near;  // evaluated world point in mm; does not imply planar type
  bool matches(const kernel::FaceInfo& face) const {
    if (!planar) return true;
    if (!face.plane) return false;
    const auto n = face.plane->normal;
    constexpr auto angular = math::Tolerance::standard().angular;
    const auto aligned = [&](math::Vec3 v) { return math::length(math::cross(n, v)) <= angular; };
    return (!normal || (math::dot(n, *normal) > 0 && aligned(*normal))) &&
           (!parallel || std::abs(math::dot(n, *parallel)) <= angular) &&
           (!perpendicular || aligned(*perpendicular));
  }
};
PlaneFilter plane_filter(const Value& where, const std::string& pointer) {
  PlaneFilter result;
  for (const auto* key : {"radius", "convex", "concave"})
    if (where.find(key)) fail("kernel", "face filter is not implemented", pointer + "/" + key);
  if (const auto* type = where.find("type")) {
    if (string(*type) != "plane")
      fail("kernel", "only planar surface filters are implemented", pointer + "/type");
    result.planar = true;
  }
  if (const auto* near = where.find("near")) {
    const auto& v = *near->if_array();
    result.near = math::Vec3{*v[0].if_number(), *v[1].if_number(), *v[2].if_number()};
  }
  for (const auto& [key, output] : {std::pair{"normal", &result.normal},
                                    {"parallel_to", &result.parallel},
                                    {"perpendicular_to", &result.perpendicular}}) {
    const auto* value = where.find(key);
    if (!value) continue;
    math::Vec3 direction;
    if (const auto* word = value->if_string()) {
      const double sign = word->front() == '-' ? -1 : 1;
      if (word->back() == 'x') direction.x = sign;
      if (word->back() == 'y') direction.y = sign;
      if (word->back() == 'z') direction.z = sign;
    } else {
      const auto& v = *value->if_array();
      direction = {*v[0].if_number(), *v[1].if_number(), *v[2].if_number()};
    }
    *output = math::normalized(direction);
    if (!*output) fail("kernel", "direction must be nonzero and finite", pointer + "/" + key);
    result.planar = true;
  }
  return result;
}
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
  const auto curve_bytes = [](const auto& curve) {
    return sizeof(curve) + 128 + curve.points().size() * sizeof(math::Vec2) +
           (curve.weights().size() + curve.knots().size()) * sizeof(double);
  };
  for (const auto& point : sketch.points) bytes += sizeof(point) + 64 + point.first.size();
  for (const auto& line : sketch.lines) bytes += sizeof(line) + 64 + line.first.size();
  for (const auto& path : sketch.paths) {
    bytes += sizeof(path);
    for (const auto& curve : path) bytes += curve_bytes(curve);
  }
  for (const auto& curve : sketch.segments) bytes += curve_bytes(curve);
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
  const auto version = "evaluator/21;roles/4;" + std::string(state_->kernel.identity());
  Evaluation result;
  result.features.reserve(document->features.size());
  Targets previous;
  std::map<std::string, std::size_t> features;
  std::map<std::string, std::shared_ptr<const Output>> sketches;
  bool rolled_back = false;
  for (std::size_t index = 0; index < document->features.size(); ++index) {
    const auto& feature = document->features[index];
    const bool profile_feature = feature.type == "extrude" || feature.type == "revolve";
    const bool pattern_feature = feature.type == "pattern_linear" ||
                                 feature.type == "pattern_circular" || feature.type == "mirror";
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
        // A selector can bind multiple current bodies at one reference pointer.
        dependencies.emplace(pointer + "/body/" + id, *body.hash);
        bindings.push_back(body.shape);
      };
      auto bind_sketch = [&](const std::string& id, const std::string& pointer) {
        const auto& upstream = result.features[features.at(id)];
        if (!upstream.hash)
          fail(upstream.status == FeatureStatus::kSuppressed ? "input_suppressed" : "input_failed",
               "sketch is unavailable: " + id, pointer);
        dependencies.emplace(pointer, *upstream.hash);
        return sketches.at(id);
      };
      std::shared_ptr<const Output> sketch, axis_sketch;
      std::optional<BodyPattern> pattern;
      std::optional<kernel::Plane> mirror;
      std::optional<kernel::Plane> selected_plane;
      std::string result_mode;
      if (profile_feature) {
        const auto& id = string(*inputs.find("profile")->find("sketch"));
        sketch = bind_sketch(id, "/inputs/profile/sketch");
        for (const auto* mode : {"join", "cut", "intersect"}) {
          if (!inputs.find("result")->find(mode)) continue;
          result_mode = mode;
          bind(affected.front(), "/inputs/result/" + result_mode);
        }
      } else if (feature.type == "transform" || pattern_feature) {
        if (feature.type == "pattern_linear") pattern = take(prepare_linear_pattern(inputs));
        if (feature.type == "mirror") mirror = take(prepare_mirror(inputs));
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
      if (feature.type == "sketch") {
        const auto& plane = *inputs.find("plane");
        if (!plane.find("datum") && !plane.find("origin")) {
          std::map<kernel::ShapePtr, std::vector<kernel::FaceInfo>> face_queries;
          auto describe = [&](const kernel::ShapePtr& shape,
                              const std::string& pointer) -> const std::vector<kernel::FaceInfo>& {
            if (const auto found = face_queries.find(shape); found != face_queries.end())
              return found->second;
            ++result.kernel_calls;
            auto queried = state_->kernel.faces(shape);
            if (!queried) fail(queried.error().code, queried.error().message, pointer);
            for (const auto& face : *queried)
              if (!face.entity || face.entity->owner() != shape ||
                  face.entity->kind() != kernel::EntityKind::kFace)
                fail("kernel", "face query returned an invalid snapshot handle", pointer);
            return face_queries.emplace(shape, std::move(*queried)).first->second;
          };
          auto inspect = [&](const kernel::EntityPtr& entity,
                             const std::string& pointer) -> const kernel::FaceInfo& {
            const auto& described = describe(entity->owner(), pointer);
            const auto found = std::ranges::find_if(
                described, [&](const auto& face) { return face.entity->same_as(*entity); });
            if (found == described.end())
              fail("kernel", "selected face is absent from its owning snapshot", pointer);
            return *found;
          };
          std::function<std::vector<kernel::EntityPtr>(const Value&, const std::string&)> select;
          select = [&](const Value& node, const std::string& pointer) {
            for (const auto* key : {"from", "edges_of"})
              if (node.find(key))
                fail("kernel", "selector field is not implemented", pointer + "/" + key);
            const auto* where = node.find("where");
            const auto filter = where ? plane_filter(*where, pointer + "/where") : PlaneFilter{};
            std::vector<kernel::EntityPtr> matches;
            auto append = [&](const kernel::EntityPtr& entity) {
              if (std::ranges::none_of(matches, [&](const auto& m) { return m->same_as(*entity); }))
                matches.push_back(entity);
            };
            if (const auto* any = node.find("any")) {
              const auto& children = *any->if_array();
              for (std::size_t i = 0; i < children.size(); ++i)
                for (const auto& entity :
                     select(children[i], pointer + "/any/" + std::to_string(i)))
                  append(entity);
            } else {
              const bool body_selector = node.find("faces_of");
              const std::string reference = body_selector ? "faces_of" : "created_by";
              const auto& id = string(*node.find(reference));
              if (!body_selector) {
                const auto& producer = result.features[features.at(id)];
                if (!producer.hash)
                  fail(producer.status == FeatureStatus::kSuppressed ? "input_suppressed"
                                                                     : "input_failed",
                       "selected producer is unavailable: " + id, pointer + "/created_by");
                dependencies.emplace(pointer + "/created_by/feature/" + id, *producer.hash);
              }
              const auto body_ids = body_selector ? std::vector{id} : previous.at(id);
              for (const auto& body_id : body_ids) {
                bind(body_id, pointer + "/" + reference);
                const auto& body = result.bodies.at(body_id);
                if (body_selector) {
                  for (const auto& face : describe(body.shape, pointer)) append(face.entity);
                } else {
                  for (const auto& name : body.names) {
                    if (name.feature != id) continue;
                    if (const auto* role = node.find("role"); role && name.role != string(*role))
                      continue;
                    if (!name.entity || name.entity->owner() != body.shape)
                      fail("kernel", "selected name belongs to a different snapshot", pointer);
                    if (name.entity->kind() == kernel::EntityKind::kFace) append(name.entity);
                  }
                }
              }
            }
            if (const auto* except = node.find("except")) {
              const auto excluded = select(*except, pointer + "/except");
              std::erase_if(matches, [&](const auto& entity) {
                return std::ranges::any_of(excluded,
                                           [&](const auto& e) { return e->same_as(*entity); });
              });
            }
            if (where)
              std::erase_if(matches, [&](const auto& entity) {
                return !filter.matches(inspect(entity, pointer));
              });
            if (filter.near) {
              std::vector<double> distances;
              double minimum = std::numeric_limits<double>::infinity();
              const auto near_pointer = pointer + "/where/near";
              for (const auto& entity : matches) {
                ++result.kernel_calls;
                const auto distance = state_->kernel.face_distance(entity, *filter.near);
                if (!distance) fail(distance.error().code, distance.error().message, near_pointer);
                if (!std::isfinite(*distance) || *distance < 0)
                  fail("kernel", "face distance must be finite and nonnegative", near_pointer);
                distances.push_back(*distance);
                minimum = std::min(minimum, *distance);
              }
              std::vector<kernel::EntityPtr> nearest;
              for (std::size_t i = 0; i < matches.size(); ++i)
                if (distances[i] - minimum <= math::Tolerance::standard().linear)
                  nearest.push_back(matches[i]);
              matches = std::move(nearest);
            }
            const auto* expect = node.find("expect");
            const std::string expectation = expect ? string(*expect) : "some";
            if ((expectation == "one" && matches.size() != 1) ||
                (expectation == "some" && matches.empty()))
              fail("selector_count",
                   "selector expected " + expectation + "; matched " +
                       std::to_string(matches.size()),
                   pointer);
            return matches;
          };
          const std::string pointer = "/inputs/plane";
          const auto matches = select(plane, pointer);
          if (matches.size() != 1)
            fail("selector_count",
                 "sketch plane requires one face; selector matched " +
                     std::to_string(matches.size()),
                 pointer);
          if (face_queries.contains(matches.front()->owner())) {
            const auto& face = inspect(matches.front(), pointer);
            if (!face.plane) fail("kernel", "selected face is not planar", pointer);
            selected_plane = face.plane;
          } else {
            ++result.kernel_calls;
            auto resolved = state_->kernel.face_plane(matches.front());
            if (!resolved) fail(resolved.error().code, resolved.error().message, pointer);
            selected_plane = *resolved;
          }
        }
      }
      const Value* axis = (feature.type == "revolve" || feature.type == "pattern_circular")
                              ? inputs.find("axis")
                              : nullptr;
      if (feature.type == "transform")
        if (const auto* rotation = inputs.find("rotation")) axis = rotation->find("axis");
      if (axis && axis->find("sketch"))
        axis_sketch = bind_sketch(
            string(*axis->find("sketch")),
            feature.type == "transform" ? "/inputs/rotation/axis/sketch" : "/inputs/axis/sketch");
      const auto* axis_input = axis_sketch ? &*axis_sketch->sketch : nullptr;
      if (feature.type == "pattern_circular")
        pattern = take(prepare_circular_pattern(inputs, axis_input));
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
          built->sketch = take(prepare_sketch(inputs, selected_plane ? &*selected_plane : nullptr));
          built->bytes += sketch_bytes(*built->sketch);
          for (const auto& input : bindings) built->bytes += input->estimated_bytes();
        } else if (profile_feature) {
          auto solid = [&] {
            if (feature.type == "extrude") {
              const auto operation = take(prepare_extrusion(inputs, *sketch->sketch));
              ++result.kernel_calls;
              return take(state_->kernel.extrude(operation));
            }
            const auto operation = take(prepare_revolution(inputs, *sketch->sketch, axis_input));
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
        } else if (pattern_feature) {
          const auto count = pattern ? pattern->count : std::size_t{2};
          built->copies.resize(bindings.size());
          built->bytes += bindings.size() * sizeof(built->copies.front());
          for (std::size_t body = 0; body < bindings.size(); ++body) {
            const auto& seed = bindings[body];
            if (count == 1) {
              ++result.kernel_calls;
              built->operations.push_back(take(state_->kernel.transform(seed, {})));
              continue;
            }
            std::vector<kernel::ShapePtr> instances;
            for (std::size_t i = 1; i < count; ++i) {
              kernel::RigidTransform placement;
              if (pattern) {
                placement = pattern->step;
                placement.translation = placement.translation * static_cast<double>(i);
                placement.angle *= static_cast<double>(i);
              }
              ++result.kernel_calls;
              auto copy = mirror ? take(state_->kernel.mirror(seed, *mirror))
                                 : take(state_->kernel.transform(seed, placement));
              for (const auto& h : copy.history)
                for (const auto& entity : h.modified)
                  if (entity && entity->kind() == kernel::EntityKind::kFace) {
                    copy.generated.push_back({"instance", entity, {}});
                    copy.estimated_bytes += sizeof(kernel::Generated) + 64;
                  }
              instances.push_back(copy.shape);
              built->bytes += sizeof(copy) + copy.estimated_bytes;
              built->copies[body].push_back(std::move(copy));
            }
            ++result.kernel_calls;
            built->operations.push_back(
                take(state_->kernel.boolean_operation(seed, instances, kernel::BooleanOp::kUnion)));
          }
        } else {
          const auto operation = take(prepare_transform(inputs, axis_input));
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
        std::vector<BodyState> copied;
        if (!output->copies.empty())
          for (const auto& copy : output->copies[i])
            copied.push_back({copy.shape, {}, names({input}, copy, feature.id), {}});
        for (const auto& copy : copied) named_inputs.push_back(&copy);
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
      const bool keep_tools = keep && *keep->if_bool();
      if (!tools.empty())
        for (auto& [producer, ids] : previous) {
          if (std::ranges::none_of(
                  ids, [&](const auto& id) { return std::ranges::find(tools, id) != tools.end(); }))
            continue;
          std::set<std::string> current(ids.begin(), ids.end());
          if (!keep_tools)
            for (const auto& tool : tools) current.erase(tool);
          current.insert(affected.front());
          ids.assign(current.begin(), current.end());
        }
      if (!keep_tools)
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
