// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "model_inputs.hpp"

#include <algorithm>
#include <map>
#include <numbers>
#include <numeric>
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
void pattern_targets(const Value& inputs, std::size_t count) {
  if (inputs.find("features")) fail("/features", "feature-target patterns are not implemented");
  const auto& targets = *field(inputs, "bodies").if_array();
  if (targets.size() > 64 / count)
    fail("/bodies", "body pattern exceeds the initial 64-total-instance work limit");
  std::set<std::string> bodies;
  for (std::size_t i = 0; i < targets.size(); ++i)
    if (!bodies.insert(*targets[i].if_string()).second)
      fail("/bodies/" + std::to_string(i), "body targets must be unique");
}
std::size_t pattern_count(const Value& inputs) {
  const double evaluated_count = number(field(inputs, "count"));
  if (evaluated_count > 64) fail("/count", "body pattern exceeds the initial 64-instance limit");
  const auto count = static_cast<std::size_t>(evaluated_count);
  pattern_targets(inputs, count);
  return count;
}
kernel::Plane prepare_plane(const Value& plane) {
  kernel::Plane result;
  if (const auto* datum = plane.find("datum")) {
    if (*datum->if_string() == "yz") result = {{}, {0, 1, 0}, {1, 0, 0}};
    if (*datum->if_string() == "zx") result = {{}, {0, 0, 1}, {0, 1, 0}};
  } else if (plane.find("origin")) {
    result = {vector3(field(plane, "origin")),
              unit(field(plane, "x_direction"), "/plane/x_direction"),
              unit(field(plane, "normal"), "/plane/normal")};
    if (!math::near(math::dot(result.x_axis, result.normal), 0, kTolerance.angular))
      fail("/plane", "plane axes must be perpendicular");
  } else {
    fail("/plane", "selector-based planes are not implemented");
  }
  return result;
}
std::pair<Vec3, Vec3> prepare_axis(const Value& axis, const Sketch* sketch,
                                   const std::string& pointer) {
  if (axis.find("origin"))
    return {vector3(field(axis, "origin")), unit(field(axis, "direction"), pointer + "/direction")};
  if (!axis.find("sketch")) fail(pointer, "selector axes are not implemented");
  if (!sketch) fail(pointer + "/sketch", "axis sketch is unavailable");
  const auto found = sketch->lines.find(*field(axis, "entity").if_string());
  if (found == sketch->lines.end()) fail(pointer + "/entity", "axis must name a sketch line");
  const auto& line = found->second;
  const auto local = math::normalized(line.end - line.start, kTolerance);
  if (!local) fail(pointer + "/entity", "axis line must be finite and nonzero");
  const auto& plane = sketch->plane;
  const auto y = math::cross(plane.normal, plane.x_axis);
  const auto origin = plane.origin + plane.x_axis * line.start.x + y * line.start.y;
  // Transform the local direction before placement; subtracting placed endpoints can
  // erase short lines when a sketch is far from the world origin.
  const auto direction = math::normalized(plane.x_axis * local->x + y * local->y, kTolerance);
  if (!direction || !std::isfinite(origin.x) || !std::isfinite(origin.y) ||
      !std::isfinite(origin.z))
    fail(pointer + "/entity", "axis exceeds finite world coordinates");
  return {origin, *direction};
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
void join_curves(std::vector<math::NurbsCurve2> curves,
                 std::vector<std::vector<math::NurbsCurve2>>& paths) {
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
      if (matches == 0) fail("/profile", "curve chain is open");
      if (matches != 1) fail("/profile", "curve junction is ambiguous");
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
        if (found != curves.end()) fail("/profile", "curve junction is ambiguous");
        found = it;
        reverse = b;
      }
      if (found == curves.end()) fail("/profile", "curve chain is open");
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
    paths.push_back(std::move(path));
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

void verify_constraints(const Value& inputs) {
  const auto& constraints = *field(inputs, "constraints").if_array();
  if (constraints.empty()) return;
  if (constraints.size() > 4096)
    fail("/constraints", "sketch exceeds the initial 4096-constraint work limit");
  const auto& entities = *field(inputs, "entities").if_array();
  if (entities.size() > 4096)
    fail("/entities/4096", "sketch exceeds the initial 4096-curve work limit");
  std::map<std::string_view, const Value*> by_id;
  for (const auto& entity : entities) by_id.emplace(*field(entity, "id").if_string(), &entity);
  for (std::size_t i = 0; i < constraints.size(); ++i) {
    const auto& constraint = constraints[i];
    const auto& kind = *field(constraint, "type").if_string();
    const auto pointer = "/constraints/" + std::to_string(i);
    if (kind != "horizontal" && kind != "vertical" && kind != "parallel" &&
        kind != "perpendicular" && kind != "equal" && kind != "radius" && kind != "coincident" &&
        kind != "distance" && kind != "midpoint" && kind != "symmetric" && kind != "tangent" &&
        kind != "angle" && kind != "fixed")
      fail(pointer + "/type", "constraint is not implemented: " + kind);
    const auto reference = [&](std::size_t index) {
      return pointer + "/entities/" + std::to_string(index);
    };
    const auto operand = [&](std::size_t index) -> const Value& {
      return field(constraint, "entities").if_array()->at(index);
    };
    const auto entity = [&](std::size_t index) -> const Value& {
      const auto& r = operand(index);
      const auto& id = r.if_string() ? *r.if_string() : *field(r, "entity").if_string();
      const auto found = by_id.find(id);
      if (found == by_id.end())
        fail(reference(index) + (r.if_string() ? "" : "/entity"),
             "unknown constraint entity: " + id);
      return *found->second;
    };
    const auto whole = [&](std::size_t index) -> const Value& {
      if (!operand(index).if_string())
        fail(reference(index) + "/point", kind + " constraint requires an entire entity");
      return entity(index);
    };
    const auto line = [&](std::size_t index) {
      const auto& e = whole(index);
      if (*field(e, "type").if_string() != "line")
        fail(reference(index), kind + " constraint requires a line");
      const auto delta = point2(field(e, "end")) - point2(field(e, "start"));
      const double length = math::length(delta);
      if (!(length > kTolerance.linear && std::isfinite(length)))
        fail(reference(index), "constraint requires a finite nonzero line");
      return delta;
    };
    const auto radius = [&](std::size_t index) {
      const auto& e = whole(index);
      const auto& type = *field(e, "type").if_string();
      if (type != "circle" && type != "arc")
        fail(reference(index), kind + " constraint requires a circle or arc");
      const double r = number(field(e, "radius"));
      if (!(r > kTolerance.linear && std::isfinite(r)))
        fail(reference(index), "constraint requires a finite positive radius");
      return r;
    };
    const auto point = [&](std::size_t index) -> Vec2 {
      const auto& e = entity(index);
      const auto& type = *field(e, "type").if_string();
      const auto* role = operand(index).find("point");
      if (!role) {
        if (type == "point") return point2(field(e, "at"));
        fail(reference(index), "point reference must specify start/end/center or name a point");
      }
      const auto& name = *role->if_string();
      if (name == "center" && (type == "circle" || type == "arc" || type == "ellipse"))
        return point2(field(e, "center"));
      if (name != "center" && type == "line") return point2(field(e, name));
      if (name != "center" && type == "arc") {
        constexpr double turn = 2 * std::numbers::pi;
        const double r = number(field(e, "radius")), sweep = number(field(e, "sweep_angle"));
        if (!(r > kTolerance.linear && std::abs(sweep) > kTolerance.angular &&
              std::abs(sweep) <= turn))
          fail(reference(index) + "/point",
               "arc point reference requires a valid radius and sweep");
        const double angle =
            std::remainder(number(field(e, "start_angle")), turn) + (name == "end" ? sweep : 0);
        const auto p = point2(field(e, "center")) + Vec2{std::cos(angle), std::sin(angle)} * r;
        if (!std::isfinite(p.x) || !std::isfinite(p.y))
          fail(reference(index) + "/point", "point reference exceeds finite coordinates");
        return p;
      }
      fail(reference(index) + "/point", name + " point is not supported for " + type);
    };
    const auto midpoint = [](Vec2 a, Vec2 b) {
      return Vec2{std::midpoint(a.x, b.x), std::midpoint(a.y, b.y)};
    };
    double residual = 0, tolerance = kTolerance.linear;
    if (kind == "fixed") {
      // Coordinates are already authored values; fixing them does not freeze parameter edits.
      // Resolve the reference now; all whole entities still undergo geometry validation below.
      if (operand(0).if_string())
        (void)whole(0);
      else
        (void)point(0);
    } else if (kind == "tangent") {
      const Value* curves[] = {&whole(0), &whole(1)};
      bool lines[2]{};
      constexpr double turn = 2 * std::numbers::pi;
      for (std::size_t j = 0; j < 2; ++j) {
        const auto& type = *field(*curves[j], "type").if_string();
        lines[j] = type == "line";
        if (!lines[j] && type != "circle" && type != "arc")
          fail(reference(j), "tangent constraint requires a line, circle or arc");
        if (type == "arc") {
          const double sweep = number(field(*curves[j], "sweep_angle"));
          if (!(std::abs(sweep) > kTolerance.angular && std::abs(sweep) <= turn))
            fail(reference(j), "tangent arc requires a nonzero sweep of at most one full turn");
        }
      }
      const auto on_arc = [&](std::size_t j, Vec2 radial) {
        if (*field(*curves[j], "type").if_string() != "arc") return;
        const double start = std::remainder(number(field(*curves[j], "start_angle")), turn),
                     sweep = number(field(*curves[j], "sweep_angle"));
        double angle = std::copysign(1.0, sweep) *
                       std::remainder(std::atan2(radial.y, radial.x) - start, turn);
        if (angle < -kTolerance.angular) angle += turn;
        if (angle > std::abs(sweep) + kTolerance.angular)
          fail(reference(j), "tangent contact lies outside the arc");
      };
      if (lines[0] && lines[1])
        fail(pointer, "tangent constraint requires at least one circle or arc");
      if (lines[0] || lines[1]) {
        const std::size_t l = lines[0] ? 0 : 1, c = 1 - l;
        const auto delta = line(l);
        const double length = math::length(delta), r = radius(c);
        const auto direction = delta / length;
        const auto offset =
            point2(field(*curves[c], "center")) - point2(field(*curves[l], "start"));
        const double along = math::dot(offset, direction),
                     distance = math::cross(direction, offset);
        if (!std::isfinite(along) || !std::isfinite(distance))
          fail(pointer, "tangent calculation exceeds finite coordinates");
        const double outside = along < 0 ? -along : std::max(0.0, along - length);
        residual = std::max(std::abs(std::abs(distance) - r), outside);
        on_arc(c, Vec2{direction.y, -direction.x} * std::copysign(1.0, distance));
      } else {
        const double a = radius(0), b = radius(1);
        const auto delta =
            point2(field(*curves[1], "center")) - point2(field(*curves[0], "center"));
        const double distance = math::length(delta);
        if (!std::isfinite(distance))
          fail(pointer, "tangent calculation exceeds finite coordinates");
        if (!(distance > kTolerance.linear))
          fail(pointer, "tangent circles require distinct centers beyond linear tolerance");
        // Subtract before summing radii to avoid overflow for large internal tangencies.
        const double external = std::abs((distance - a) - b),
                     internal = std::abs(distance - std::abs(a - b));
        const auto direction = delta / distance;
        const bool outside = external <= internal;
        residual = outside ? external : internal;
        const auto radial = direction * (outside || a >= b ? 1.0 : -1.0);
        on_arc(0, radial);
        on_arc(1, outside ? -radial : radial);
      }
    } else if (kind == "coincident" || kind == "distance") {
      const double expected = kind == "distance" ? number(field(constraint, "value")) : 0;
      if (expected < 0) fail(pointer + "/value", "distance dimension must be nonnegative");
      const auto a = point(0), b = point(1);
      residual = std::abs(math::length(a - b) - expected);
    } else if (kind == "midpoint") {
      const std::size_t l =
          operand(0).if_string() && *field(entity(0), "type").if_string() == "line" ? 0 : 1;
      (void)line(l);
      const auto& e = entity(l);
      residual =
          math::length(point(1 - l) - midpoint(point2(field(e, "start")), point2(field(e, "end"))));
    } else if (kind == "symmetric") {
      const auto direction = *math::normalized(line(2), kTolerance);
      const auto a = point(0), b = point(1), anchor = point2(field(entity(2), "start"));
      // Components of b minus the reflection of a; midpoint avoids adding huge coordinates.
      residual = std::hypot(math::dot(b - a, direction),
                            2 * math::cross(direction, midpoint(a, b) - anchor));
    } else if (kind == "radius") {
      const double expected = number(field(constraint, "value"));
      if (!(expected > 0)) fail(pointer + "/value", "radius dimension must be positive");
      residual = std::abs(radius(0) - expected);
    } else if (kind == "equal") {
      residual = *field(whole(0), "type").if_string() == "line"
                     ? std::abs(math::length(line(0)) - math::length(line(1)))
                     : std::abs(radius(0) - radius(1));
    } else {
      // Compare directions, not endpoint offsets: the angular bound is independent of size.
      const auto a = *math::normalized(line(0), kTolerance);
      tolerance = kTolerance.angular;
      if (kind == "horizontal")
        residual = std::atan2(std::abs(a.y), std::abs(a.x));
      else if (kind == "vertical")
        residual = std::atan2(std::abs(a.x), std::abs(a.y));
      else {
        const auto b = *math::normalized(line(1), kTolerance);
        const double cross = std::abs(math::cross(a, b)), dot = math::dot(a, b);
        if (kind == "angle") {
          const double expected = number(field(constraint, "value"));
          if (!(expected >= 0 && expected <= std::numbers::pi))
            fail(pointer + "/value", "angle dimension must be between zero and pi radians");
          residual = std::abs(std::atan2(cross, dot) - expected);
        } else {
          residual = kind == "parallel" ? std::atan2(cross, std::abs(dot))
                                        : std::atan2(std::abs(dot), cross);
        }
      }
    }
    if (!std::isfinite(residual))
      fail(pointer, "constraint calculation exceeds finite coordinates");
    if (!(residual <= tolerance)) {
      const auto unit = tolerance == kTolerance.linear ? " mm" : " rad";
      fail(pointer, kind + " constraint is not satisfied: residual " +
                        ir::json::format_number(residual) + unit + ", tolerance " +
                        ir::json::format_number(tolerance) + unit);
    }
  }
}
}  // namespace

std::expected<Sketch, ir::Error> prepare_sketch(const Value& inputs,
                                                const kernel::Plane* selected_plane) {
  return guarded([&] {
    verify_constraints(inputs);
    Sketch sketch;
    sketch.plane = selected_plane ? *selected_plane : prepare_plane(field(inputs, "plane"));
    const auto add_curve = [&](math::NurbsCurve2 curve) {
      if (math::near(curve.point(curve.domain_start()), curve.point(curve.domain_end()),
                     kTolerance))
        sketch.paths.push_back({std::move(curve)});
      else
        sketch.segments.push_back(std::move(curve));
    };
    const auto& entities = *field(inputs, "entities").if_array();
    std::size_t curves = 0, control_points = 0;
    for (std::size_t i = 0; i < entities.size(); ++i) {
      const auto& entity = entities[i];
      const auto pointer = "/entities/" + std::to_string(i);
      const auto& type = *field(entity, "type").if_string();
      if (++curves > 4096) fail(pointer, "sketch exceeds the initial 4096-curve work limit");
      if (type == "point") {
        sketch.points.emplace(*field(entity, "id").if_string(), point2(field(entity, "at")));
      } else if (type == "circle") {
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
        sketch.segments.push_back(math::make_line(a, b));
        sketch.lines.emplace(*field(entity, "id").if_string(), Sketch::Line{a, b});
      } else {
        fail(pointer + "/type", "sketch entity is not implemented: " + type);
      }
    }
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
    auto closed_paths = sketch.paths;
    join_curves(sketch.segments, closed_paths);
    // Arrangement probes must lie on a smooth span, not an interior spline corner.
    // Exact Bézier decomposition also exposes intersections between different spans.
    std::vector<std::vector<math::NurbsCurve2>> paths;
    std::size_t spans = 0;
    for (const auto& path : closed_paths) {
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
    if (regions->empty()) fail("/profile", "profile has no filled regions");
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
                                                                const Sketch& sketch,
                                                                const Sketch* axis_sketch) {
  auto regions = prepare_regions(inputs, sketch);
  if (!regions) return std::unexpected(regions.error());
  return guarded([&] {
    const auto [origin, direction] = prepare_axis(field(inputs, "axis"), axis_sketch, "/axis");
    return kernel::Revolution{sketch.plane, std::move(*regions), origin, direction,
                              number(field(inputs, "angle"))};
  });
}

std::expected<kernel::RigidTransform, ir::Error> prepare_transform(const Value& inputs,
                                                                   const Sketch* axis_sketch) {
  return guarded([&] {
    std::set<std::string> bodies;
    for (const auto& body : *field(inputs, "bodies").if_array())
      if (!bodies.insert(*body.if_string()).second) fail("/bodies", "body targets must be unique");
    kernel::RigidTransform result;
    if (const auto* translation = inputs.find("translation"))
      result.translation = vector3(*translation);
    if (const auto* rotation = inputs.find("rotation")) {
      const auto [origin, direction] =
          prepare_axis(field(*rotation, "axis"), axis_sketch, "/rotation/axis");
      result.axis_origin = origin;
      result.axis_direction = direction;
      result.angle = number(field(*rotation, "angle"));
    }
    return result;
  });
}

std::expected<BodyPattern, ir::Error> prepare_linear_pattern(const Value& inputs) {
  return guarded([&] {
    const auto count = pattern_count(inputs);
    const double spacing = number(field(inputs, "spacing"));
    if (count > 1 && !(std::abs(spacing) > kTolerance.linear))
      fail("/spacing", "pattern spacing must exceed the linear tolerance in magnitude");
    const auto step = unit(field(inputs, "direction"), "/direction") * spacing;
    const auto last = step * static_cast<double>(count - 1);
    if (!std::isfinite(last.x) || !std::isfinite(last.y) || !std::isfinite(last.z))
      fail("/spacing", "pattern offsets exceed finite coordinates");
    kernel::RigidTransform placement;
    placement.translation = step;
    return BodyPattern{count, placement};
  });
}

std::expected<BodyPattern, ir::Error> prepare_circular_pattern(const Value& inputs,
                                                               const Sketch* axis_sketch) {
  return guarded([&] {
    const auto count = pattern_count(inputs);
    const double sweep = number(field(inputs, "angle")), turn = 2 * std::numbers::pi;
    if (std::abs(sweep) > turn + kTolerance.angular)
      fail("/angle", "circular pattern sweep must be at most one full turn");
    const bool full = std::abs(sweep) >= turn - kTolerance.angular;
    const double step = full ? std::copysign(turn, sweep) / static_cast<double>(count)
                             : sweep / static_cast<double>(std::max(std::size_t{1}, count - 1));
    if (count > 1 && !(std::abs(step) > kTolerance.angular))
      fail("/angle", "pattern angular step must exceed the angular tolerance in magnitude");
    const auto [origin, direction] = prepare_axis(field(inputs, "axis"), axis_sketch, "/axis");
    kernel::RigidTransform placement;
    placement.axis_origin = origin;
    placement.axis_direction = direction;
    placement.angle = count == 1 ? 0 : step;
    return BodyPattern{count, placement};
  });
}
std::expected<kernel::Plane, ir::Error> prepare_mirror(const Value& inputs,
                                                       const kernel::Plane* selected_plane) {
  return guarded([&] {
    pattern_targets(inputs, 2);
    return selected_plane ? *selected_plane : prepare_plane(field(inputs, "plane"));
  });
}
}  // namespace nf::kernel_service
