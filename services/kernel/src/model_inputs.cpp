// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "model_inputs.hpp"

#include <set>

#include "nf/math/constructors.hpp"

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
void join_lines(std::vector<std::pair<Vec2, Vec2>> lines, Sketch& sketch) {
  while (!lines.empty()) {
    const auto [start, second] = lines.back();
    lines.pop_back();
    auto end = second;
    std::vector<math::NurbsCurve2> path{math::make_line(start, end)};
    while (!math::near(start, end, kTolerance)) {
      auto found = lines.end();
      bool reverse = false;
      for (auto it = lines.begin(); it != lines.end(); ++it) {
        const bool a = math::near(it->first, end, kTolerance),
                   b = math::near(it->second, end, kTolerance);
        if (!a && !b) continue;
        if (found != lines.end()) fail("/entities", "line junction is ambiguous");
        found = it;
        reverse = b;
      }
      if (found == lines.end()) fail("/entities", "line chain is open");
      const auto next = reverse ? found->first : found->second;
      path.push_back(math::make_line(end, next));
      end = next;
      lines.erase(found);
    }
    sketch.paths.push_back(std::move(path));
  }
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
    std::vector<std::pair<Vec2, Vec2>> lines;
    const auto& entities = *field(inputs, "entities").if_array();
    std::size_t curves = 0;
    for (std::size_t i = 0; i < entities.size(); ++i) {
      const auto& entity = entities[i];
      const auto pointer = "/entities/" + std::to_string(i);
      const auto& type = *field(entity, "type").if_string();
      if (++curves > 4096) fail(pointer, "sketch exceeds the initial 4096-curve work limit");
      if (type == "circle") {
        const auto radius = number(field(entity, "radius"));
        if (!(radius > kTolerance.linear)) fail(pointer + "/radius", "radius is too small");
        sketch.paths.push_back({math::make_circle(point2(field(entity, "center")), radius)});
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
        lines.emplace_back(a, b);
      } else {
        fail(pointer + "/type", "sketch entity is not implemented: " + type);
      }
    }
    join_lines(std::move(lines), sketch);
    return sketch;
  });
}

std::expected<kernel::Extrusion, ir::Error> prepare_extrusion(const Value& inputs,
                                                              const Sketch& sketch) {
  return guarded([&] {
    const auto& result = field(inputs, "result");
    if (result.find("new") && *field(result, "body_type").if_string() != "brep")
      fail("/result/body_type", "only B-rep bodies are implemented", "unsupported_body_type");
    if (!inputs.find("distance")) fail("/to", "extrude-to selectors are not implemented");
    if (const auto* draft = inputs.find("draft"); draft && number(*draft) != 0)
      fail("/draft", "drafted extrusion is not implemented");
    const auto& profile = field(inputs, "profile");
    auto rule = math::FillRule::kEvenOdd;
    if (const auto* fill = profile.find("fill_rule"); fill && *fill->if_string() == "nonzero")
      rule = math::FillRule::kNonZero;
    auto regions = math::regions(sketch.paths, rule);
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
    const auto direction = inputs.find("direction") ? unit(field(inputs, "direction"), "/direction")
                                                    : sketch.plane.normal;
    kernel::Extrusion extrusion{sketch.plane, std::move(*regions),
                                direction * number(field(inputs, "distance"))};
    if (const auto* symmetric = inputs.find("symmetric"); symmetric && *symmetric->if_bool())
      extrusion.plane.origin = extrusion.plane.origin - extrusion.displacement / 2;
    return extrusion;
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
