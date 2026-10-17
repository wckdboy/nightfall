// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "model_inputs.hpp"

#include <numbers>
#include <set>

#include "nf/math/constructors.hpp"
#include "nf/math/refine.hpp"

namespace nf::kernel_service {
namespace {
using ir::json::Value;
using math::Vec2;
using math::Vec3;
constexpr auto kTolerance = math::Tolerance::standard();
[[noreturn]] void fail(std::string pointer, std::string message, std::string code = "kernel") {
  throw ir::Error{std::move(code), std::move(message), "/inputs" + pointer, {}};
}
template <class F>
auto guarded(F&& f) -> std::expected<decltype(f()), ir::Error> {
  try {
    return f();
  } catch (const ir::Error& error) {
    return std::unexpected(error);
  } catch (const std::invalid_argument& error) {
    return std::unexpected(ir::Error{"kernel", error.what(), "/inputs", {}});
  }
}
const Value& field(const Value& value, std::string_view name) { return *value.find(name); }
double number(const Value& value) { return *value.if_number(); }
Vec2 point2(const Value& value) {
  const auto& a = *value.if_array();
  return {number(a[0]), number(a[1])};
}
Vec3 vector3(const Value& value) {
  if (const auto* word = value.if_string()) {
    Vec3 v;
    const double sign = word->front() == '-' ? -1.0 : 1.0;
    if (word->back() == 'x') v.x = sign;
    if (word->back() == 'y') v.y = sign;
    if (word->back() == 'z') v.z = sign;
    return v;
  }
  const auto& a = *value.if_array();
  return {number(a[0]), number(a[1]), number(a[2])};
}
Vec3 unit(const Value& value, const std::string& pointer) {
  auto result = math::normalized(vector3(value), kTolerance);
  if (!result) fail(pointer, "direction must be nonzero and finite");
  return *result;
}
std::vector<math::NurbsCurve2> polygon(std::vector<Vec2> points, const std::string& pointer) {
  if (math::near(points.front(), points.back(), kTolerance)) points.pop_back();
  if (points.size() < 3) fail(pointer, "polygon needs three distinct vertices");
  std::vector<math::NurbsCurve2> result;
  for (std::size_t i = 0; i < points.size(); ++i) {
    const auto next = points[(i + 1) % points.size()];
    if (math::near(points[i], next, kTolerance)) fail(pointer, "zero-length polygon edge");
    result.push_back(math::make_line(points[i], next));
  }
  return result;
}
void join_curves(std::vector<math::NurbsCurve2> curves, Sketch& sketch) {
  // Check both ends before walking: stopping at the first closed loop could hide a
  // multi-way junction at its starting vertex. Closed single curves are separate paths.
  std::vector<std::pair<Vec2, Vec2>> ends;
  for (const auto& curve : curves)
    ends.emplace_back(curve.point(curve.domain_start()), curve.point(curve.domain_end()));
  for (std::size_t i = 0; i < ends.size(); ++i) {
    for (const auto p : {ends[i].first, ends[i].second}) {
      std::size_t matches = 0;
      for (std::size_t j = 0; j < ends.size(); ++j) {
        if (i == j) continue;
        matches += math::near(p, ends[j].first, kTolerance);
        matches += math::near(p, ends[j].second, kTolerance);
      }
      if (matches == 0) fail("/entities", "curve chain is open");
      if (matches != 1) fail("/entities", "curve junction is ambiguous");
    }
  }
  while (!curves.empty()) {
    const auto start = curves.back().point(curves.back().domain_start());
    auto end = curves.back().point(curves.back().domain_end());
    std::vector<math::NurbsCurve2> path{std::move(curves.back())};
    curves.pop_back();
    while (!math::near(start, end, kTolerance)) {
      auto found = curves.end();
      bool reverse = false;
      for (auto it = curves.begin(); it != curves.end(); ++it) {
        const bool a = math::near(it->point(it->domain_start()), end, kTolerance),
                   b = math::near(it->point(it->domain_end()), end, kTolerance);
        if (!a && !b) continue;
        if (found != curves.end()) fail("/entities", "curve junction is ambiguous");
        found = it;
        reverse = b;
      }
      if (found == curves.end()) fail("/entities", "curve chain is open");
      auto next = reverse ? math::reverse(*found) : *found;
      const auto next_start = next.point(next.domain_start());
      // Keep the original curves exact and explicitly connect tolerance-matched ends.
      // Arrangement vertex welding may use a tighter, drawing-relative tolerance.
      if (end != next_start) path.push_back(math::make_line(end, next_start));
      path.push_back(std::move(next));
      end = path.back().point(path.back().domain_end());
      curves.erase(found);
    }
    if (end != start) path.push_back(math::make_line(end, start));
    sketch.paths.push_back(std::move(path));
  }
}
math::NurbsCurve2 spline(const Value& entity, const std::string& pointer,
                         std::size_t& control_points) {
  const auto& poles = *field(entity, "control_points").if_array();
  if (poles.size() > 4096 - control_points)
    fail(pointer + "/control_points", "sketch exceeds the initial 4096-spline-pole work limit");
  control_points += poles.size();
  const auto degree = static_cast<int>(number(field(entity, "degree")));
  const auto& values = *field(entity, "knots").if_array();
  if (values.size() != poles.size() + static_cast<std::size_t>(degree) + 1)
    fail(pointer + "/knots", "knot count must equal control point count + degree + 1");
  std::vector<double> knots, weights;
  for (const auto& v : values) knots.push_back(number(v));
  auto kv = math::KnotVector::make(std::move(knots), degree);
  if (!kv) fail(pointer + "/knots", kv.error());
  if (!std::isfinite(kv->domain_start() + kv->domain_end()) ||
      !std::isfinite(kv->domain_end() - kv->domain_start()))
    fail(pointer + "/knots", "spline domain exceeds finite arithmetic for reversal and splitting");
  if (const auto* w = entity.find("weights")) {
    if (w->if_array()->size() != poles.size())
      fail(pointer + "/weights", "need one weight per control point");
    for (const auto& v : *w->if_array()) weights.push_back(number(v));
  }
  std::vector<Vec2> points;
  for (const auto& p : poles) points.push_back(point2(p));
  auto curve = math::NurbsCurve2::make(std::move(*kv), std::move(points), std::move(weights));
  if (!curve)
    fail(pointer + (entity.find("weights") ? "/weights" : "/control_points"), curve.error());
  return std::move(*curve);
}
}  // namespace

std::expected<Sketch, ir::Error> prepare_sketch(const Value& inputs) {
  return guarded([&] {
    if (!field(inputs, "constraints").if_array()->empty())
      fail("/constraints", "constraint verification is not implemented");
    Sketch sketch;
    const auto& plane = field(inputs, "plane");
    if (const auto* datum = plane.find("datum")) {
      if (*datum->if_string() == "yz") sketch.plane = {{}, {0, 1, 0}, {1, 0, 0}};
      if (*datum->if_string() == "zx") sketch.plane = {{}, {0, 0, 1}, {0, 1, 0}};
    } else if (plane.find("origin")) {
      sketch.plane = {vector3(field(plane, "origin")),
                      unit(field(plane, "x_direction"), "/plane/x_direction"),
                      unit(field(plane, "normal"), "/plane/normal")};
      if (!math::near(math::dot(sketch.plane.x_axis, sketch.plane.normal), 0, kTolerance.angular))
        fail("/plane", "plane axes must be perpendicular");
    } else {
      fail("/plane", "selector-based sketch planes are not implemented");
    }
    std::vector<math::NurbsCurve2> segments;
    const auto add_curve = [&](math::NurbsCurve2 curve) {
      if (math::near(curve.point(curve.domain_start()), curve.point(curve.domain_end()),
                     kTolerance))
        sketch.paths.push_back({std::move(curve)});
      else
        segments.push_back(std::move(curve));
    };
    const auto& entities = *field(inputs, "entities").if_array();
    std::size_t curves = 0, control_points = 0;
    for (std::size_t i = 0; i < entities.size(); ++i) {
      const auto& entity = entities[i];
      const auto pointer = "/entities/" + std::to_string(i);
      const auto& type = *field(entity, "type").if_string();
      if (++curves > 4096) fail(pointer, "sketch exceeds the initial 4096-curve work limit");
      if (type == "circle") {
        const auto radius = number(field(entity, "radius"));
        if (!(radius > kTolerance.linear)) fail(pointer + "/radius", "radius is too small");
        sketch.paths.push_back({math::make_circle(point2(field(entity, "center")), radius)});
      } else if (type == "arc") {
        const auto radius = number(field(entity, "radius"));
        if (!(radius > kTolerance.linear)) fail(pointer + "/radius", "radius is too small");
        constexpr auto turn = 2 * std::numbers::pi;
        const auto sweep = number(field(entity, "sweep_angle"));
        if (!(std::abs(sweep) > kTolerance.angular && std::abs(sweep) <= turn))
          fail(pointer + "/sweep_angle", "arc sweep must be nonzero and at most one full turn");
        const auto start = std::remainder(number(field(entity, "start_angle")), turn);
        auto curve =
            math::make_arc(point2(field(entity, "center")), radius, radius, 0,
                           sweep > 0 ? start : start + sweep, sweep > 0 ? start + sweep : start);
        add_curve(sweep > 0 ? std::move(curve) : math::reverse(curve));
      } else if (type == "ellipse") {
        const auto radii = point2(field(entity, "radii"));
        if (!(radii.x > kTolerance.linear && radii.y > kTolerance.linear))
          fail(pointer + "/radii", "ellipse radii must be positive");
        const auto* rotation = entity.find("rotation");
        add_curve(
            math::make_arc(point2(field(entity, "center")), radii.x, radii.y,
                           rotation ? std::remainder(number(*rotation), 2 * std::numbers::pi) : 0,
                           0, 2 * std::numbers::pi));
      } else if (type == "spline") {
        add_curve(spline(entity, pointer, control_points));
      } else if (type == "rect") {
        const auto p = point2(field(entity, "corner")), size = point2(field(entity, "size"));
        if (!(size.x > kTolerance.linear && size.y > kTolerance.linear))
          fail(pointer + "/size", "rectangle sizes must be positive");
        sketch.paths.push_back(
            polygon({p, {p.x + size.x, p.y}, p + size, {p.x, p.y + size.y}}, pointer));
        curves += 3;
        if (curves > 4096) fail(pointer, "sketch exceeds the initial 4096-curve work limit");
      } else if (type == "polygon") {
        std::vector<Vec2> points;
        for (const auto& p : *field(entity, "points").if_array()) points.push_back(point2(p));
        curves += points.size() - 1;
        if (curves > 4096) fail(pointer, "sketch exceeds the initial 4096-curve work limit");
        sketch.paths.push_back(polygon(std::move(points), pointer));
      } else if (type == "line") {
        const auto a = point2(field(entity, "start")), b = point2(field(entity, "end"));
        if (math::near(a, b, kTolerance)) fail(pointer, "zero-length line");
        segments.push_back(math::make_line(a, b));
      } else {
        fail(pointer + "/type", "sketch entity is not implemented: " + type);
      }
    }
    join_curves(std::move(segments), sketch);
    return sketch;
  });
}

namespace {
std::expected<std::vector<math::Region>, ir::Error> prepare_regions(const Value& inputs,
                                                                    const Sketch& sketch) {
  return guarded([&] {
    const auto& result = field(inputs, "result");
    if (result.find("new") && *field(result, "body_type").if_string() != "brep")
      fail("/result/body_type", "only B-rep bodies are implemented", "unsupported_body_type");
    const auto& profile = field(inputs, "profile");
    auto rule = math::FillRule::kEvenOdd;
    if (const auto* fill = profile.find("fill_rule"); fill && *fill->if_string() == "nonzero")
      rule = math::FillRule::kNonZero;
    // Arrangement probes must lie on a smooth span, not an interior spline corner.
    // Exact Bézier decomposition also exposes intersections between different spans.
    std::vector<std::vector<math::NurbsCurve2>> paths;
    std::size_t spans = 0;
    for (const auto& path : sketch.paths) {
      paths.emplace_back();
      for (const auto& curve : path) {
        auto pieces = math::decompose(curve);
        spans += pieces.size();
        if (spans > 4096) fail("/profile", "profile exceeds the initial 4096-span work limit");
        paths.back().insert(paths.back().end(), pieces.begin(), pieces.end());
      }
    }
    auto regions = math::regions(paths, rule);
    if (!regions) fail("/profile", regions.error());
    if (const auto* picks = profile.find("regions")) {
      std::set<std::size_t> selected;
      for (std::size_t p = 0; p < picks->if_array()->size(); ++p) {
        const auto pointer = "/profile/regions/" + std::to_string(p);
        const auto point = point2((*picks->if_array())[p]);
        std::optional<std::size_t> found;
        for (std::size_t r = 0; r < regions->size(); ++r) {
          auto winding = math::winding_number((*regions)[r].outer.curves, point);
          if (!winding) fail(pointer, "region point lies on a boundary");
          bool inside = *winding != 0;
          for (const auto& hole : (*regions)[r].holes) {
            winding = math::winding_number(hole.curves, point);
            if (!winding) fail(pointer, "region point lies on a boundary");
            if (*winding != 0) inside = false;
          }
          if (inside) {
            if (found) fail(pointer, "region point is ambiguous");
            found = r;
          }
        }
        if (!found) fail(pointer, "region point is outside the filled profile");
        selected.insert(*found);
      }
      std::vector<math::Region> picked;
      for (auto index : selected) picked.push_back(std::move((*regions)[index]));
      *regions = std::move(picked);
    }
    return std::move(*regions);
  });
}
}  // namespace

std::expected<kernel::Extrusion, ir::Error> prepare_extrusion(const Value& inputs,
                                                              const Sketch& sketch) {
  auto regions = prepare_regions(inputs, sketch);
  if (!regions) return std::unexpected(regions.error());
  return guarded([&] {
    if (!inputs.find("distance")) fail("/to", "extrude-to selectors are not implemented");
    if (const auto* draft = inputs.find("draft"); draft && number(*draft) != 0)
      fail("/draft", "drafted extrusion is not implemented");
    const auto direction = inputs.find("direction") ? unit(field(inputs, "direction"), "/direction")
                                                    : sketch.plane.normal;
    kernel::Extrusion extrusion{sketch.plane, std::move(*regions),
                                direction * number(field(inputs, "distance"))};
    if (const auto* symmetric = inputs.find("symmetric"); symmetric && *symmetric->if_bool())
      extrusion.plane.origin = extrusion.plane.origin - extrusion.displacement / 2;
    return extrusion;
  });
}

std::expected<kernel::Revolution, ir::Error> prepare_revolution(const Value& inputs,
                                                                const Sketch& sketch) {
  auto regions = prepare_regions(inputs, sketch);
  if (!regions) return std::unexpected(regions.error());
  return guarded([&] {
    const auto& axis = field(inputs, "axis");
    if (!axis.find("origin")) fail("/axis", "selector and sketch axes are not implemented");
    return kernel::Revolution{sketch.plane, std::move(*regions), vector3(field(axis, "origin")),
                              unit(field(axis, "direction"), "/axis/direction"),
                              number(field(inputs, "angle"))};
  });
}

std::expected<kernel::RigidTransform, ir::Error> prepare_transform(const Value& inputs) {
  return guarded([&] {
    std::set<std::string> bodies;
    for (const auto& body : *field(inputs, "bodies").if_array())
      if (!bodies.insert(*body.if_string()).second) fail("/bodies", "body targets must be unique");
    kernel::RigidTransform result;
    if (const auto* translation = inputs.find("translation"))
      result.translation = vector3(*translation);
    if (const auto* rotation = inputs.find("rotation")) {
      const auto& axis = field(*rotation, "axis");
      if (!axis.find("origin"))
        fail("/rotation/axis", "selector and sketch axes are not implemented");
      result.axis_origin = vector3(field(axis, "origin"));
      result.axis_direction = unit(field(axis, "direction"), "/rotation/axis/direction");
      result.angle = number(field(*rotation, "angle"));
    }
    return result;
  });
}
}  // namespace nf::kernel_service
