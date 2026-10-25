// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "urdf.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <ranges>

#include "assembly_queries.hpp"

namespace nf::kernel_service {
namespace {
using R3 = std::array<double, 9>;
/// A rigid transform: x' = rotation x + translation (row-major rotation, mm).
struct Pose {
  R3 r{1, 0, 0, 0, 1, 0, 0, 0, 1};
  math::Vec3 t;
};
Pose pose(const Placement& p) { return {p.rotation, p.translation}; }
math::Vec3 apply(const R3& r, const math::Vec3& v) {
  return {r[0] * v.x + r[1] * v.y + r[2] * v.z, r[3] * v.x + r[4] * v.y + r[5] * v.z,
          r[6] * v.x + r[7] * v.y + r[8] * v.z};
}
R3 multiply(const R3& a, const R3& b) {
  R3 c{};
  for (std::size_t i = 0; i < 3; ++i)
    for (std::size_t j = 0; j < 3; ++j)
      for (std::size_t k = 0; k < 3; ++k) c[3 * i + j] += a[3 * i + k] * b[3 * k + j];
  return c;
}
R3 transpose(const R3& a) { return {a[0], a[3], a[6], a[1], a[4], a[7], a[2], a[5], a[8]}; }
Pose compose(const Pose& a, const Pose& b) {
  const auto t = apply(a.r, b.t);
  return {multiply(a.r, b.r), {a.t.x + t.x, a.t.y + t.y, a.t.z + t.z}};
}
Pose inverse(const Pose& a) {
  const auto rt = transpose(a.r);
  const auto t = apply(rt, a.t);
  return {rt, {-t.x, -t.y, -t.z}};
}
/// URDF's fixed-axis roll, pitch, yaw: R = Rz(yaw) Ry(pitch) Rx(roll).
std::array<double, 3> rpy(const R3& r) {
  const double pitch = std::asin(std::clamp(-r[6], -1.0, 1.0));
  if (std::abs(r[6]) < 1 - 1e-12) return {std::atan2(r[7], r[8]), pitch, std::atan2(r[3], r[0])};
  return {0.0, pitch, std::atan2(-r[1], r[4])};  // gimbal lock: all of the turn in yaw
}
std::string number(double v) { return ir::json::format_number(std::abs(v) < 1e-15 ? 0.0 : v); }
std::string triple(double x, double y, double z) {
  return number(x) + " " + number(y) + " " + number(z);
}
/// <origin> of a pose, in metres.
std::string origin(const Pose& p) {
  const auto a = rpy(p.r);
  return "<origin xyz=\"" + triple(p.t.x * 1e-3, p.t.y * 1e-3, p.t.z * 1e-3) + "\" rpy=\"" +
         triple(a[0], a[1], a[2]) + "\"/>";
}
ir::Errors refuse(const std::string& pointer, const std::string& message) {
  return {{"unsupported", message, pointer, {}}};
}
}  // namespace

std::expected<UrdfExport, ir::Errors> export_urdf(kernel::IKernel& kernel,
                                                  const ir::Document& document,
                                                  const AssemblyResult& result,
                                                  const std::map<std::string, double>& density,
                                                  const std::string& mesh_dir,
                                                  const std::string& name) {
  const auto& occurrences = result.occurrences;
  if (occurrences.empty())
    return std::unexpected(
        ir::Errors{{"not_an_assembly", "the document has no occurrences", "/occurrences", {}}});
  UrdfExport out;
  std::size_t root = 0;
  for (std::size_t i = 0; i < document.occurrences.size(); ++i)
    if (document.occurrences[i].grounded.value_or(false)) {
      root = i;
      break;
    }
  // Spanning tree in joint order; link frames are the child-side joint frames.
  std::vector<Pose> link(occurrences.size());
  std::vector<bool> linked(occurrences.size(), false);
  link[root] = pose(occurrences[root].placement);
  linked[root] = true;
  std::string joints_xml;
  const auto world = [&](std::size_t occurrence, const Placement& frame) {
    return compose(pose(occurrences[occurrence].placement), pose(frame));
  };
  for (bool grew = true; grew;) {
    grew = false;
    for (std::size_t k = 0; k < result.joints.size(); ++k) {
      const auto& j = result.joints[k];
      if (linked[j.a] == linked[j.b]) continue;
      const auto pointer = "/joints/" + std::to_string(k);
      const bool child_is_b = linked[j.a];
      const std::size_t parent = child_is_b ? j.a : j.b, child = child_is_b ? j.b : j.a;
      std::string type;
      if (j.kind == "revolute")
        type = j.limits ? "revolute" : "continuous";
      else if (j.kind == "slider" && j.limits)
        type = "prismatic";
      else if (j.kind == "slider")
        // URDF reads a prismatic joint without bounds as locked at 0: a slider needs limits.
        return std::unexpected(refuse(pointer + "/limits",
                                      "a slider needs limits to export as a URDF prismatic joint"));
      else if (j.kind == "rigid")
        type = "fixed";
      else
        return std::unexpected(refuse(pointer + "/kind", j.kind + " joints have no URDF form yet"));
      const auto parent_frame = world(parent, child_is_b ? j.frame_a : j.frame_b);
      link[child] = world(child, child_is_b ? j.frame_b : j.frame_a);
      linked[child] = true;
      grew = true;
      // Frame b is frame a moved by the joint value along or about a's z: driving the child
      // end a runs the same motion backwards.
      joints_xml += "  <joint name=\"" + j.id + "\" type=\"" + type + "\">\n    <parent link=\"" +
                    occurrences[parent].id + "\"/>\n    <child link=\"" + occurrences[child].id +
                    "\"/>\n    " + origin(compose(inverse(link[parent]), parent_frame)) + "\n";
      if (type != "fixed") {
        joints_xml += std::string("    <axis xyz=\"0 0 ") + (child_is_b ? "1" : "-1") + "\"/>\n";
        const bool rotary = type != "prismatic";
        const double scale = rotary ? 1.0 : 1e-3;
        joints_xml += "    <limit";
        if (j.limits)
          joints_xml += " lower=\"" + number(j.limits->first * scale) + "\" upper=\"" +
                        number(j.limits->second * scale) + "\"";
        joints_xml += rotary ? " effort=\"10\" velocity=\"3.14159\"/>\n"
                             : " effort=\"100\" velocity=\"0.1\"/>\n";
        out.warnings.push_back(j.id + ": default effort and velocity limits");
      }
      joints_xml += "  </joint>\n";
    }
  }
  for (std::size_t k = 0; k < result.joints.size(); ++k)
    if (std::ranges::count(out.cut_joints, result.joints[k].id) == 0 &&
        joints_xml.find("name=\"" + result.joints[k].id + "\"") == std::string::npos)
      out.cut_joints.push_back(result.joints[k].id);
  for (std::size_t i = 0; i < occurrences.size(); ++i)
    if (!linked[i])
      return std::unexpected(ir::Errors{
          {"unsupported",
           "occurrence " + occurrences[i].id + " is not joined to the grounded occurrence",
           "/occurrences/" + std::to_string(i),
           {}}});
  std::string links_xml;
  for (std::size_t i = 0; i < occurrences.size(); ++i) {
    const auto& o = occurrences[i];
    const auto placement = pose(o.placement);
    std::vector<PlacedBody> bodies;
    for (const auto& [id, shape] : o.bodies) bodies.emplace_back(o.id + "/" + id, shape);
    double rho = 1000;
    if (const auto d = density.find(o.id); d != density.end())
      rho = d->second;
    else
      out.warnings.push_back(o.id + ": no density, 1000 kg/m^3 assumed");
    const auto mass = mass_properties(kernel, bodies, {{o.id, rho}});
    if (!mass) return std::unexpected(mass.error());
    const auto& m = mass->total;
    const auto inv = inverse(link[i]);
    const auto com = apply(inv.r, m.centroid);
    const auto inertia = multiply(multiply(inv.r, m.inertia), transpose(inv.r));
    // The mesh, in metres in the occurrence's own frame: v = R^T (v_world - t).
    std::string obj = "# Nightfall derived mesh of occurrence " + o.id + " (m)\n";
    std::size_t offset = 1;
    const auto local = inverse(placement);
    for (const auto& [id, shape] : o.bodies) {
      const auto mesh = kernel.tessellate(shape, {});
      if (!mesh)
        return std::unexpected(ir::Errors{{mesh.error().code, mesh.error().message, "", {}}});
      for (const auto& v : mesh->vertices) {
        const auto p = compose(local, Pose{{}, v.position}).t;
        obj += "v " + triple(p.x * 1e-3, p.y * 1e-3, p.z * 1e-3) + "\n";
      }
      for (const auto& v : mesh->vertices) {
        const auto n = apply(local.r, v.normal);
        obj += "vn " + triple(n.x, n.y, n.z) + "\n";
      }
      for (std::size_t t = 0; t + 2 < mesh->indices.size(); t += 3) {
        obj += "f";
        for (std::size_t c = 0; c < 3; ++c) {
          const auto index = std::to_string(offset + mesh->indices[t + c]);
          obj += " " + index + "//" + index;
        }
        obj += "\n";
      }
      offset += mesh->vertices.size();
    }
    const auto mesh_path = mesh_dir + "/" + o.id + ".obj";
    out.meshes.emplace_back(mesh_path, std::move(obj));
    const auto visual = origin(compose(inv, placement));
    links_xml += "  <link name=\"" + o.id + "\">\n    <inertial>\n      <origin xyz=\"" +
                 triple(com.x * 1e-3 + inv.t.x * 1e-3, com.y * 1e-3 + inv.t.y * 1e-3,
                        com.z * 1e-3 + inv.t.z * 1e-3) +
                 "\" rpy=\"0 0 0\"/>\n      <mass value=\"" + number(m.mass) +
                 "\"/>\n      <inertia ixx=\"" + number(inertia[0]) + "\" ixy=\"" +
                 number(inertia[1]) + "\" ixz=\"" + number(inertia[2]) + "\" iyy=\"" +
                 number(inertia[4]) + "\" iyz=\"" + number(inertia[5]) + "\" izz=\"" +
                 number(inertia[8]) + "\"/>\n    </inertial>\n";
    for (const auto* tag : {"visual", "collision"})
      links_xml += std::string("    <") + tag + ">\n      " + visual +
                   "\n      <geometry><mesh filename=\"" + mesh_path + "\"/></geometry>\n    </" +
                   tag + ">\n";
    links_xml += "  </link>\n";
  }
  for (const auto& id : out.cut_joints)
    out.warnings.push_back(id + ": closes a loop; URDF is a tree, so it is not exported");
  out.urdf =
      "<?xml version=\"1.0\"?>\n<!-- Nightfall RFC 0013 export: metres, kilograms, "
      "radians. Geometry is the part document; meshes are derived. -->\n<robot name=\"" +
      name + "\">\n" + links_xml + joints_xml + "</robot>\n";
  return out;
}
}  // namespace nf::kernel_service
