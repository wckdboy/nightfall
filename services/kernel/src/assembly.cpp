// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "assembly.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>

#include "nf/assembly/solve.h"
#include "nf/ir/parameters.hpp"

namespace nf::kernel_service {
namespace {
using ir::json::Value;
using math::Vec3;

[[noreturn]] void fail(std::string pointer, std::string message, std::string code = "kernel") {
  throw ir::Errors{{std::move(code), std::move(message), std::move(pointer), {}}};
}
template <class T, class E>
T take(std::expected<T, E> result, const std::string& pointer) {
  if (result) return std::move(*result);
  if constexpr (std::is_same_v<E, kernel::KernelError>)
    fail(pointer, result.error().message, result.error().code);
  else if constexpr (std::is_same_v<E, ir::Error>)
    fail(pointer + result.error().pointer, result.error().message, result.error().code);
  else
    throw result.error();
}

Vec3 scale(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
Vec3 add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
std::optional<Vec3> unit(Vec3 a) {
  const double n = std::sqrt(dot(a, a));
  if (!(n > 1e-12) || !std::isfinite(n)) return {};
  return scale(a, 1 / n);
}

/// Rotation about the fixed x, then y, then z axes (RFC 0013 placements and offsets).
std::array<double, 9> rotation_xyz(double rx, double ry, double rz) {
  const double cx = std::cos(rx), sx = std::sin(rx), cy = std::cos(ry), sy = std::sin(ry),
               cz = std::cos(rz), sz = std::sin(rz);
  // R = Rz * Ry * Rx
  return {cz * cy,
          cz * sy * sx - sz * cx,
          cz * sy * cx + sz * sx,
          sz * cy,
          sz * sy * sx + cz * cx,
          sz * sy * cx - cz * sx,
          -sy,
          cy * sx,
          cy * cx};
}
Vec3 apply(const std::array<double, 9>& r, Vec3 v) {
  return {r[0] * v.x + r[1] * v.y + r[2] * v.z, r[3] * v.x + r[4] * v.y + r[5] * v.z,
          r[6] * v.x + r[7] * v.y + r[8] * v.z};
}

/// The values of a {translation, rotation} object: three lengths and three angles.
std::pair<Vec3, Vec3> transform_values(const Value* transform, const ir::Units& units,
                                       const ir::ParameterValues& params,
                                       const std::string& pointer) {
  Vec3 t, r;
  if (!transform) return {t, r};
  const auto read = [&](const char* key, ir::Dimension dimension, Vec3& out) {
    const auto* list = transform->find(key);
    if (!list) return;
    const auto& a = *list->if_array();
    double v[3];
    for (std::size_t k = 0; k < 3; ++k)
      v[k] = take(ir::evaluate_input(a.at(k), dimension, units, params),
                  pointer + "/" + key + "/" + std::to_string(k));
    out = {v[0], v[1], v[2]};
  };
  read("translation", ir::kLength, t);
  read("rotation", ir::kAngle, r);
  return {t, r};
}

/// RFC 0013 frame rules for the single face a `{created_by, role, from}` selector names.
nf_frame derive_frame(kernel::IKernel& kernel, const Evaluation& evaluation,
                      const ir::JointEnd& end, const ir::Units& units,
                      const ir::ParameterValues& params, const std::string& pointer) {
  const auto* object = end.frame.if_object();
  for (const auto& member : *object)
    if (member.key != "created_by" && member.key != "role" && member.key != "from" &&
        member.key != "expect" && !member.key.starts_with("x-"))
      fail(pointer + "/frame/" + member.key,
           "joint frames support {created_by, role, from} selectors only (" + member.key + ")",
           "unsupported");
  const auto text = [&](const char* key) -> std::string {
    const auto* v = end.frame.find(key);
    return v && v->if_string() ? *v->if_string() : std::string();
  };
  const auto producer = text("created_by"), role = text("role"), from = text("from");
  // A frame is one surface: `expect` may only restate that. Patches of one surface (OCCT splits
  // periodic faces) are merged below, which keeps the centroid of a face that splits.
  if (const auto* expect = end.frame.find("expect");
      expect && !(expect->if_string() && *expect->if_string() == "one"))
    fail(pointer + "/frame/expect", "a joint frame selects one surface; expect must be \"one\"",
         "unsupported");
  std::vector<std::pair<kernel::ShapePtr, kernel::EntityPtr>> found;
  for (const auto& [id, body] : evaluation.bodies) {
    if (!body.shape || !body.unavailable.empty()) continue;
    for (const auto& name : body.names) {
      if (name.feature != producer || (!role.empty() && name.role != role) ||
          (!from.empty() && name.from != from) || !name.entity ||
          name.entity->kind() != kernel::EntityKind::kFace)
        continue;
      bool repeated = false;
      for (const auto& [shape, entity] : found)
        repeated = repeated || entity->same_as(*name.entity);
      if (!repeated) found.emplace_back(body.shape, name.entity);
    }
  }
  if (found.empty()) fail(pointer + "/frame", "joint frame selector matches no face");
  // The kernel may split one surface into patches (a hole wall into quarter cylinders): faces
  // that all lie on the same plane, or the same cylinder, are one surface with an
  // area-weighted centroid. Anything else is ambiguous.
  std::vector<kernel::FaceInfo> infos;
  double area = 0;
  Vec3 moment;
  for (const auto& [shape, entity] : found) {
    const auto faces = take(kernel.faces(shape), pointer + "/frame");
    const kernel::FaceInfo* match = nullptr;
    for (const auto& f : faces)
      if (f.entity->same_as(*entity)) match = &f;
    if (!match) fail(pointer + "/frame", "selected face is not in its body");
    infos.push_back(*match);
    const auto mass = take(kernel.face_mass(entity), pointer + "/frame");
    area += mass.area;
    moment = add(moment, scale(mass.centroid, mass.area));
  }
  const auto* info = &infos.front();
  for (const auto& other : infos) {
    bool same = false;
    if (info->plane && other.plane) {
      const auto n = info->plane->normal;
      same = math::length(cross(n, other.plane->normal)) <= 1e-9 &&
             std::abs(dot(sub(other.plane->origin, info->plane->origin), n)) <= 1e-6;
    } else if (info->cylinder && other.cylinder) {
      const auto& a = info->cylinder->axis;
      const auto& b = other.cylinder->axis;
      const auto u = *unit(a.direction);
      const auto offset = sub(b.origin, a.origin);
      same = math::length(cross(u, *unit(b.direction))) <= 1e-9 &&
             math::length(sub(offset, scale(u, dot(offset, u)))) <= 1e-6 &&
             std::abs(info->cylinder->radius - other.cylinder->radius) <= 1e-6;
    }
    if (!same)
      fail(pointer + "/frame", "joint frame selector matches " + std::to_string(found.size()) +
                                   " faces on different surfaces; a frame needs one surface");
  }
  if (!(area > 0)) fail(pointer + "/frame", "selected surface has no area");
  const auto centroid = scale(moment, 1 / area);
  const auto rule = end.rule.value_or(info->plane ? "plane_centroid" : "cylinder_axis");
  Vec3 origin, z, x;
  if (rule == "plane_centroid") {
    if (!info->plane) fail(pointer + "/rule", "plane_centroid needs a planar face");
    origin = centroid;
    z = info->plane->normal;
    x = info->plane->x_axis;
  } else if (rule == "cylinder_axis") {
    if (!info->cylinder) fail(pointer + "/rule", "cylinder_axis needs a cylindrical face");
    z = *unit(info->cylinder->axis.direction);
    const auto base = info->cylinder->axis.origin;
    origin = add(base, scale(z, dot(sub(centroid, base), z)));
    // Toward the centroid when it lies off the axis; a full cylinder's centroid is on it, so
    // the world x (else y) direction perpendicular to the axis is used instead.
    // The centroid of a full wall is on the axis up to integration noise: offsets below 1e-6
    // of the radius count as on it.
    const auto off = sub(centroid, origin);
    auto radial = math::length(off) > 1e-6 * info->cylinder->radius ? unit(off) : std::nullopt;
    if (!radial) radial = unit(sub(Vec3{1, 0, 0}, scale(z, z.x)));
    if (!radial) radial = unit(sub(Vec3{0, 1, 0}, scale(z, z.y)));
    x = *radial;
  } else {
    fail(pointer + "/rule", "frame rule is not supported yet: " + rule, "unsupported");
  }
  if (end.flip.value_or(false)) {
    z = scale(z, -1);
    x = scale(x, -1);
  }
  auto y = cross(z, x);
  const auto [t, r] =
      transform_values(end.offset ? &*end.offset : nullptr, units, params, pointer + "/offset");
  const auto rotation = rotation_xyz(r.x, r.y, r.z);
  // The offset acts in the frame's own axes.
  const auto local = [&](Vec3 v) { return add(add(scale(x, v.x), scale(y, v.y)), scale(z, v.z)); };
  origin = add(origin, local(t));
  const auto rotate = [&](Vec3 axis) { return local(apply(rotation, axis)); };
  const auto nx = rotate({1, 0, 0}), ny = rotate({0, 1, 0}), nz = rotate({0, 0, 1});
  return nf_frame{
      {origin.x, origin.y, origin.z}, {nx.x, nx.y, nx.z}, {ny.x, ny.y, ny.z}, {nz.x, nz.y, nz.z}};
}

/// Axis-angle form of a proper rotation (for the kernel's rigid transform).
std::pair<Vec3, double> axis_angle(const std::array<double, 9>& r) {
  const double trace = r[0] + r[4] + r[8];
  // The skew part is 2 sin(angle) times the axis: atan2 keeps small angles exact, where
  // acos of the trace loses about sqrt(eps).
  Vec3 axis{r[7] - r[5], r[2] - r[6], r[3] - r[1]};
  const double angle = std::atan2(0.5 * math::length(axis), 0.5 * (trace - 1));
  if (angle < 1e-15) return {{0, 0, 1}, 0};
  if (auto u = unit(axis); u && std::numbers::pi - angle > 1e-6) return {*u, angle};
  // Near a half turn: the axis is the column of R + I with the largest norm, signed by the
  // skew-symmetric part (2 sin(angle) times the axis) while that still carries a sign.
  const Vec3 c[3] = {{r[0] + 1, r[3], r[6]}, {r[1], r[4] + 1, r[7]}, {r[2], r[5], r[8] + 1}};
  std::size_t best = 0;
  for (std::size_t k = 1; k < 3; ++k)
    if (dot(c[k], c[k]) > dot(c[best], c[best])) best = k;
  auto u = *unit(c[best]);
  if (dot(u, axis) < 0) u = scale(u, -1);
  return {u, angle};
}
}  // namespace

std::expected<AssemblyResult, ir::Errors> evaluate_assembly(Evaluator& evaluator,
                                                            kernel::IKernel& kernel,
                                                            const ir::Document& document) {
  try {
    AssemblyResult result;
    if (document.occurrences.empty()) return result;
    auto parameters = ir::evaluate_parameters(document.params);
    if (!parameters) throw parameters.error();
    const auto& params = parameters->values;
    std::map<std::string, const ir::Component*> components;
    for (const auto& c : document.components) components[c.id] = &c;
    // Each distinct component and parameter set is one part document, evaluated once.
    std::map<std::string, Evaluation> evaluated;
    std::vector<const Evaluation*> per_occurrence;
    std::vector<nf_occurrence> flat;
    for (std::size_t i = 0; i < document.occurrences.size(); ++i) {
      const auto& o = document.occurrences[i];
      const auto pointer = "/occurrences/" + std::to_string(i);
      ir::Document part;
      part.units = document.units;
      part.params = document.params;
      const ir::Component* component = nullptr;
      if (o.component == "root") {
        part.features = document.features;
      } else {
        component = components.at(o.component);
        if (!component->features)
          fail(pointer + "/component", "referenced components are not supported yet",
               "unsupported");
        part.features = *component->features;
      }
      for (const auto* overrides : {component ? &component->params : nullptr, &o.params}) {
        if (!overrides) continue;
        for (const auto& [name, value] : *overrides)
          for (auto& p : part.params)
            if (p.name == name) p.value = value;
      }
      auto text = ir::write_document(part);
      if (!text) throw text.error();
      auto [entry, inserted] = evaluated.try_emplace(*text);
      if (inserted) {
        auto evaluation = evaluator.evaluate(*text);
        if (!evaluation) throw evaluation.error();
        if (!evaluation->ok()) {
          std::string first;
          for (const auto& f : evaluation->features)
            if (!f.errors.empty() && first.empty()) first = f.errors.front().message;
          fail(pointer + "/component", "component does not build: " + first);
        }
        result.kernel_calls += evaluation->kernel_calls;
        result.cache_hits += evaluation->cache_hits;
        result.binding_misses += evaluation->binding_misses;
        for (const auto& f : evaluation->features) result.component_features.push_back(f.status);
        entry->second = std::move(*evaluation);
      }
      per_occurrence.push_back(&entry->second);
      const auto [t, r] = transform_values(o.placement ? &*o.placement : nullptr, document.units,
                                           params, pointer + "/placement");
      nf_occurrence c{};
      c.grounded = o.grounded.value_or(false) ? 1 : 0;
      const auto rotation = rotation_xyz(r.x, r.y, r.z);
      std::copy(rotation.begin(), rotation.end(), c.placement.rotation);
      c.placement.translation[0] = t.x;
      c.placement.translation[1] = t.y;
      c.placement.translation[2] = t.z;
      flat.push_back(c);
    }
    std::map<std::string, std::uint32_t> occurrence_index;
    for (std::size_t i = 0; i < document.occurrences.size(); ++i)
      occurrence_index[document.occurrences[i].id] = static_cast<std::uint32_t>(i);
    static const std::map<std::string, std::pair<std::uint32_t, std::vector<ir::Dimension>>> kinds{
        {"rigid", {NF_JOINT_RIGID, {}}},
        {"revolute", {NF_JOINT_REVOLUTE, {ir::kAngle}}},
        {"slider", {NF_JOINT_SLIDER, {ir::kLength}}},
        {"cylindrical", {NF_JOINT_CYLINDRICAL, {ir::kAngle, ir::kLength}}},
        {"pin_slot", {NF_JOINT_PIN_SLOT, {ir::kAngle, ir::kLength}}},
        {"planar", {NF_JOINT_PLANAR, {ir::kLength, ir::kLength, ir::kAngle}}},
        {"ball", {NF_JOINT_BALL, {}}}};
    std::vector<nf_joint> joints;
    for (std::size_t k = 0; k < document.joints.size(); ++k) {
      const auto& j = document.joints[k];
      const auto pointer = "/joints/" + std::to_string(k);
      nf_joint c{};
      const auto& [kind, dimensions] = kinds.at(j.kind);
      c.kind = kind;
      const auto end = [&](const ir::JointEnd& e, const char* side, std::uint32_t& index,
                           nf_frame& frame) {
        const auto found = occurrence_index.find(e.occurrence);
        if (found == occurrence_index.end())
          fail(pointer + "/" + side + "/occurrence",
               "nested occurrence paths are not supported yet", "unsupported");
        index = found->second;
        frame = derive_frame(kernel, *per_occurrence[index], e, document.units, params,
                             pointer + "/" + side);
      };
      end(j.a, "a", c.a, c.frame_a);
      end(j.b, "b", c.b, c.frame_b);
      // One value for a single degree of freedom, or a list with one value or null each.
      if (j.value) {
        const auto drive = [&](std::size_t d, const Value& v) {
          if (v.is_null()) return;
          if (d >= dimensions.size()) fail(pointer + "/value", "too many joint values");
          c.driven[d] = 1;
          c.values[d] = take(ir::evaluate_input(v, dimensions[d], document.units, params),
                             pointer + "/value");
        };
        if (const auto* list = j.value->if_array())
          for (std::size_t d = 0; d < list->size(); ++d) drive(d, (*list)[d]);
        else
          drive(0, *j.value);
      }
      if (j.limits) {
        const auto& list = *j.limits->if_array();
        // One [low, high] for a single degree of freedom, or a list of them (or null).
        const bool single =
            dimensions.size() == 1 && list.size() == 2 && !list[0].if_array() && !list[0].is_null();
        const auto limit = [&](std::size_t d, const Value& range) {
          if (range.is_null()) return;
          if (!range.if_array() || range.if_array()->size() != 2)
            fail(pointer + "/limits", "a limit is [lower, upper]");
          const auto& pair = *range.if_array();
          c.limited[d] = 1;
          c.lower[d] =
              take(ir::evaluate_input(pair.at(0), dimensions.at(d), document.units, params),
                   pointer + "/limits");
          c.upper[d] =
              take(ir::evaluate_input(pair.at(1), dimensions.at(d), document.units, params),
                   pointer + "/limits");
        };
        if (single)
          limit(0, *j.limits);
        else
          for (std::size_t d = 0; d < list.size() && d < 3; ++d) limit(d, list[d]);
      }
      joints.push_back(c);
    }
    std::vector<nf_placement> placements(flat.size());
    std::vector<double> mm(joints.size()), rad(joints.size());
    std::vector<std::uint8_t> redundant(joints.size()), conflicting(joints.size());
    nf_assembly_report report{};
    std::size_t error_index = 0;
    const auto status = nf_assembly_solve(
        flat.data(), flat.size(), joints.data(), joints.size(), placements.data(), mm.data(),
        rad.data(), redundant.data(), conflicting.data(), &report, &error_index);
    if (status != NF_ASSEMBLY_OK) {
      const auto where =
          status == NF_ASSEMBLY_INVALID_PLACEMENT
              ? "/occurrences/" + std::to_string(error_index)
              : (error_index < joints.size() ? "/joints/" + std::to_string(error_index)
                                             : std::string("/joints"));
      fail(where, "assembly solver rejected the input (status " + std::to_string(status) + ")");
    }
    if (!report.solved) {
      std::string named, first;
      for (std::size_t k = 0; k < conflicting.size(); ++k)
        if (conflicting[k]) {
          const auto pointer = "/joints/" + std::to_string(k);
          if (first.empty()) first = pointer;
          named += (named.empty() ? "" : ", ") + pointer;
        }
      fail(first.empty() ? "/joints" : first, "joints conflict: " + named);
    }
    result.solved = true;
    result.dof = report.dof;
    result.loops = report.loops;
    result.iterations = report.iterations;
    const auto local = [](const nf_frame& f) {
      Placement p;
      for (std::size_t r = 0; r < 3; ++r) {
        p.rotation[3 * r] = f.x[r];
        p.rotation[3 * r + 1] = f.y[r];
        p.rotation[3 * r + 2] = f.z[r];
      }
      p.translation = {f.origin[0], f.origin[1], f.origin[2]};
      return p;
    };
    for (std::size_t k = 0; k < joints.size(); ++k) {
      const auto& c = joints[k];
      AssemblyJoint joint{document.joints[k].id,
                          mm[k],
                          rad[k],
                          redundant[k] != 0,
                          conflicting[k] != 0,
                          document.joints[k].kind,
                          c.a,
                          c.b,
                          local(c.frame_a),
                          local(c.frame_b),
                          std::nullopt};
      if (c.limited[0]) joint.limits = std::pair{c.lower[0], c.upper[0]};
      result.joints.push_back(std::move(joint));
    }
    for (std::size_t i = 0; i < flat.size(); ++i) {
      AssemblyOccurrence occurrence{document.occurrences[i].id, {}, {}, {}};
      std::copy(std::begin(placements[i].rotation), std::end(placements[i].rotation),
                occurrence.placement.rotation.begin());
      occurrence.placement.translation = {
          placements[i].translation[0], placements[i].translation[1], placements[i].translation[2]};
      const auto [axis, angle] = axis_angle(occurrence.placement.rotation);
      const kernel::RigidTransform transform{occurrence.placement.translation, {}, axis, angle};
      for (const auto& [id, body] : per_occurrence[i]->bodies) {
        if (!body.shape || !body.unavailable.empty()) continue;
        auto placed =
            take(kernel.transform(body.shape, transform), "/occurrences/" + std::to_string(i));
        occurrence.bodies.emplace_back(id, placed.shape);
        occurrence.source_hashes.push_back(body.hash);
      }
      result.occurrences.push_back(std::move(occurrence));
    }
    return result;
  } catch (ir::Errors& errors) {
    return std::unexpected(std::move(errors));
  }
}
}  // namespace nf::kernel_service
