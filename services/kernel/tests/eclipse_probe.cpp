// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
// Private headless stress test/exporter. This does not bypass or change XPC deadlines.
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <STEPControl_Reader.hxx>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <memory>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "evaluator.hpp"
#include "kernel_bridge.h"
#include "snapshot.hpp"

using namespace nf;
using namespace nf::kernel_service;
namespace {
constexpr double pi = std::numbers::pi;
using Clock = std::chrono::steady_clock;
void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}
void near(double value, double expected, double tolerance, const std::string& message) {
  if (std::isfinite(value) && std::abs(value - expected) <= tolerance) return;
  std::ostringstream error;
  error << std::setprecision(17) << message << ": got " << value << ", expected " << expected
        << ", absolute tolerance " << tolerance;
  throw std::runtime_error(error.str());
}
double milliseconds(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
std::string read(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  require(file.good(), "cannot read " + path.string());
  return {std::istreambuf_iterator<char>{file}, {}};
}
void write(const std::filesystem::path& path, const void* bytes, std::size_t count) {
  std::ofstream file(path, std::ios::binary);
  if (count) file.write(static_cast<const char*>(bytes), static_cast<std::streamsize>(count));
  require(file.good(), "cannot write " + path.string());
}
std::array<double, 3> polynomial(std::array<double, 3> values) {
  return {values[0], -3 * values[0] + 4 * values[1] - values[2],
          2 * values[0] - 4 * values[1] + 2 * values[2]};
}
double product(std::array<double, 3> a, std::array<double, 3> b) {
  a = polynomial(a);
  b = polynomial(b);
  double sum = 0;
  for (std::size_t i = 0; i < 3; ++i)
    for (std::size_t j = 0; j < 3; ++j) sum += a[i] * b[j] / static_cast<double>(i + j + 1);
  return sum;
}
double loft(double length, std::array<double, 3> a, std::array<double, 3> b) {
  return pi * length * product(a, b);
}
double cable(bool utility, int steps) {
  double sum = 0;
  for (int i = 0; i <= steps; ++i) {
    const double t = static_cast<double>(i) / steps, s = 1 - t;
    const double dx = utility ? -420 * s * t - 210 * t * t : 1260 * s * t + 300 * t * t;
    const double dy = utility ? -270 * s * s - 360 * s * t + 204 * t * t
                              : -510 * s * s - 510 * s * t + 495 * t * t;
    sum += (i == 0 || i == steps ? 1 : (i % 2 ? 4 : 2)) * std::hypot(dx, dy);
  }
  return sum / (3 * steps);
}
double twisted_vane(double waist) {
  std::array<double, 3> ax{}, ay{}, bx{}, by{};
  const std::array<double, 3> a{28, 36 * waist, 22}, b{8, 7 * waist, 5};
  for (std::size_t i = 0; i < 3; ++i) {
    const double angle = static_cast<double>(i) * pi / 12;
    ax[i] = a[i] * std::cos(angle);
    ay[i] = a[i] * std::sin(angle);
    bx[i] = -b[i] * std::sin(angle);
    by[i] = b[i] * std::cos(angle);
  }
  // Cross-section determinant of the two quadratic ellipse basis vectors.
  return pi * 130 * (product(ax, by) - product(ay, bx));
}
std::pair<double, double> foundation() {
  constexpr double outer = 620, inner = 540, core = 150, half = 50, height = 20;
  auto integral = [](double r) {
    return half * std::sqrt(r * r - half * half) + r * r * std::asin(half / r);
  };
  const double area =
      pi * (outer * outer - inner * inner + core * core) + 4 * (integral(inner) - integral(core));
  const double perimeter =
      2 * pi * outer + (2 * pi - 8 * std::asin(half / inner)) * inner +
      (2 * pi - 8 * std::asin(half / core)) * core +
      8 * (std::sqrt(inner * inner - half * half) - std::sqrt(core * core - half * half));
  return {area * height, 2 * area + perimeter * height};
}
std::map<std::string, double> volumes(double waist) {
  auto ring = [](double r, double bore, double h, int holes = 0, int copies = 1) {
    return copies * pi * (r * r - bore * bore - holes * 2.75 * 2.75) * h;
  };
  auto rounded = [](double w, double d, double r) { return w * d - (4 - pi) * r * r; };
  for (bool utility : {false, true})
    near(cable(utility, 4096), cable(utility, 8192), 1e-8, "cable oracle convergence");
  std::map<std::string, double> result{
      {"pedestal", 196404 * pi},
      {"cooling_fins", 32 * 14 * rounded(16, 2, .6)},
      {"shoulder_housing", ring(42, 15, 110)},
      {"shoulder_covers", ring(46, 15, 8, 8, 2)},
      {"shoulder_fasteners", 16 * pi * (16 - 1.8 * 1.8) * 3},
      {"elbow_housing", ring(34, 11, 96)},
      {"elbow_covers", ring(38, 11, 8, 8, 2)},
      {"elbow_fasteners", 16 * pi * (16 - 1.8 * 1.8) * 3},
      {"wrist_housing", ring(26, 10, 70)},
      {"wrist_covers", ring(30, 10, 8, 8, 2)},
      {"wrist_fasteners", 16 * pi * (16 - 1.8 * 1.8) * 3},
      {"upper_links", 2 * loft(190, {22, 16, 19}, {12, 9, 11})},
      {"forearm", loft(160, {24, 18, 16}, {27, 20, 17})},
      {"cable_harness", 2 * pi * 3.5 * 3.5 * cable(false, 8192)},
      {"harness_base_anchor", rounded(18, 30, 3) * 16},
      {"harness_wrist_anchor", rounded(18, 46, 3) * 36},
      {"tool_neck", ring(16, 9, 18)},
      {"tool_flange", ring(28, 9, 6)},
      {"gripper_palm", (rounded(80, 44, 6) - 4 * pi * 2.75 * 2.75) * 16},
      {"gripper_jaws", 2 * (rounded(12, 28, 3) * 56 + 18 * 28 * 16)},
      {"palm_fasteners", 4 * pi * 16 * 3}};
  for (auto& [id, volume] : result) {
    (void)id;
    volume *= 4;
  }
  double rotor = 0;
  for (const auto& row :
       std::array{std::array{6., 48., 48.}, std::array{10., 48., 36.}, std::array{100., 36., 30.},
                  std::array{10., 30., 44.}, std::array{4., 44., 44.}})
    rotor += pi * row[0] * (row[1] * row[1] + row[1] * row[2] + row[2] * row[2]) / 3;
  result.insert({{"cell_floor", foundation().first},
                 {"cell_station_pads", 4 * pi * (90 * 90 - 14 * 14 - 4 * 25) * 8},
                 {"cell_stand", pi * (85 * 85 - 20 * 20) * 52},
                 {"cell_lower_flange", pi * (150 * 150 - 20 * 20 - 24 * 3.2 * 3.2) * 8},
                 {"cell_upper_flange", pi * (130 * 130 - 45 * 45 - 24 * 2.5 * 2.5) * 8},
                 {"cell_rotor", rotor - pi * 20 * 20 * 130},
                 {"cell_vanes", 12 * twisted_vane(waist)},
                 {"cell_lower_bolts", 24 * pi * ((25 - 2.3 * 2.3) * 4 - (5 + 2.3) * .5 * .5)},
                 {"cell_upper_bolts", 24 * pi * ((16 - 1.8 * 1.8) * 3 - (4 + 1.8) * .4 * .4)},
                 {"cell_deck_bolts", 32 * pi * ((36 - 2.8 * 2.8) * 8 - (6 + 2.8))},
                 {"cell_utility_cables", 4 * pi * 16 * cable(true, 8192)}});
  return result;
}
Evaluation build(Evaluator& evaluator, const std::string& source) {
  auto value = evaluator.evaluate(source);
  require(value.has_value(), "document validation failed");
  for (const auto& f : value->features)
    for (const auto& e : f.errors)
      std::fprintf(stderr, "%s: %s\n", e.pointer.c_str(), e.message.c_str());
  require(value->ok(), "feature evaluation failed");
  return std::move(*value);
}
kernel::Measures verify(const Evaluation& result, double waist, ir::json::Array* rows = nullptr) {
  const auto expected = volumes(waist);
  kernel::Measures total{};
  std::size_t live = 0;
  for (const auto& [id, body] : result.bodies) {
    if (!body.shape) continue;
    ++live;
    const auto& m = body.shape->measures();
    require(expected.contains(id), "unexpected body " + id);
    require(body.hash.has_value(), "missing body hash " + id);
    near(m.volume, expected.at(id), .02, id + " independent volume mm3");
    near(m.centroid.x, 500, 1e-6, id + " fourfold centroid x mm");
    near(m.centroid.y, 0, 1e-6, id + " fourfold centroid y mm");
    require(m.solids > 0 && m.faces > 0 && m.edges > 0 && m.area > 0, "empty topology " + id);
    for (const auto& name : body.names)
      require(name.entity && name.entity->owner() == body.shape, "stale provenance " + id);
    if (rows)
      rows->push_back(ir::json::Object{
          {"id", id},
          {"state_hash", body.hash->hex()},
          {"volume_mm3", m.volume},
          {"analytic_volume_mm3", expected.at(id)},
          {"volume_difference_mm3", m.volume - expected.at(id)},
          {"area_mm2", m.area},
          {"centroid_mm", ir::json::Array{m.centroid.x, m.centroid.y, m.centroid.z}},
          {"solids", static_cast<double>(m.solids)},
          {"faces", static_cast<double>(m.faces)},
          {"edges", static_cast<double>(m.edges)}});
    total.volume += m.volume;
    total.area += m.area;
    total.solids += m.solids;
    total.faces += m.faces;
    total.edges += m.edges;
  }
  require(live == expected.size() && live == 32 && total.solids == 529, "body/solid coverage");
  near(result.bodies.at("cell_floor").shape->measures().area, foundation().second, .01,
       "foundation independent area mm2");
  return total;
}
}  // namespace
int main(int argc, char** argv) {
  ir::json::Object failure{{"status", "failed"}};
  try {
    require(argc == 2 || argc == 3,
            "usage: nf_eclipse_probe eclipse.nightfall [new-output-directory]");
    const std::filesystem::path path = argv[1];
    const std::string source = read(path);
    failure.push_back({"fixture_sha256", ir::sha256(source).hex()});
    failure.push_back({"probe_binary_sha256", ir::sha256(read(argv[0])).hex()});
    const std::filesystem::path folder = argc == 3 ? argv[2] : "";
    if (!folder.empty()) require(!std::filesystem::exists(folder), "output already exists");
    auto backend = kernel::make_occt_kernel().value();
    failure.push_back({"kernel", std::string(backend->identity())});
    Evaluator evaluator(*backend, {96, 64 * 1024 * 1024});
    auto start = Clock::now();
    const auto original = build(evaluator, source);
    const double cold_ms = milliseconds(start);
    std::fprintf(stderr, "Eclipse: cold evaluation %.3f ms\n", cold_ms);
    require(original.features.size() == 113, "authored feature coverage");
    ir::json::Array metrics;
    const auto exact = verify(original, 1, &metrics);
    auto part = ir::read_document(source).value();
    for (auto& p : part.params)
      if (p.name == "rotor_waist") p.value = 1.05;
    verify(build(evaluator, ir::write_document(part).value()), 1.05);
    const auto restored = build(evaluator, source);
    verify(restored, 1);
    for (const auto& [id, body] : original.bodies)
      require(restored.bodies.at(id).hash == body.hash, "restored state hash " + id);
    evaluator.clear_cache();
    verify(build(evaluator, source), 1);
    verify(original, 1);
    std::vector<kernel::ShapePtr> shapes;
    for (const auto& [id, body] : original.bodies) {
      (void)id;
      if (body.shape) shapes.push_back(body.shape);
    }
    start = Clock::now();
    const auto step = backend->export_step(shapes).value();
    const double step_ms = milliseconds(start);
    std::istringstream stream(step);
    STEPControl_Reader reader;
    require(reader.ReadStream("eclipse.step", stream) == IFSelect_RetDone, "STEP read");
    reader.SetSystemLengthUnit(1);
    require(reader.TransferRoots() > 0, "STEP transfer");
    const auto loaded = kernel::detail::snapshot(reader.OneShape()).value();
    near(loaded->measures().volume, exact.volume, .05, "STEP exact volume mm3");
    near(loaded->measures().area, exact.area, .05, "STEP exact area mm2");
    require(loaded->measures().solids == exact.solids && loaded->measures().faces == exact.faces &&
                loaded->measures().edges == exact.edges,
            "STEP closed-solid topology");
    GProp_GProps reader_volume, reader_area;
    BRepGProp::VolumeProperties(reader.OneShape(), reader_volume, 1e-9, true, false);
    BRepGProp::SurfaceProperties(reader.OneShape(), reader_area, 1e-9, false);
    near(reader_volume.Mass(), exact.volume, .05, "separate STEP integration volume mm3");
    near(reader_area.Mass(), exact.area, .05, "separate STEP integration area mm2");
    using Session = std::unique_ptr<nf_session, decltype(&nf_session_destroy)>;
    using Result = std::unique_ptr<nf_result, decltype(&nf_result_free)>;
    Session session(nf_session_create(), nf_session_destroy);
    require(session != nullptr, "null session");
    start = Clock::now();
    Result packet(nf_session_build(session.get(), source.data(), source.size(), nullptr),
                  nf_result_free);
    const double packet_ms = milliseconds(start);
    require(packet != nullptr, "null packet");
    session.reset();  // All result buffers must remain valid independently of the session.
    std::size_t json_bytes = 0, nv = 0, ni = 0, nf = 0;
    const auto* json = nf_result_json(packet.get(), &json_bytes);
    const auto manifest = ir::json::parse(std::string_view(json, json_bytes)).value();
    require(manifest.find("errors")->if_array()->empty(), std::string(json, json_bytes));
    const auto* vertices = nf_result_vertices(packet.get(), &nv);
    const auto* indices = nf_result_indices(packet.get(), &ni);
    const auto* faces = nf_result_faces(packet.get(), &nf);
    require(
        nv > 0 && nv <= 3000000 && ni > 0 && ni % 3 == 0 && ni / 3 <= 1000000 && nf == exact.faces,
        "bounded mesh coverage");
    const auto& bodies = *manifest.find("bodies")->if_array();
    require(bodies.size() == 32, "packet body coverage");
    std::vector<double> mesh_volumes(bodies.size()), mesh_areas(bodies.size());
    auto vec = [](const double* p) { return math::Vec3{p[0], p[1], p[2]}; };
    for (std::size_t i = 0; i < nv; ++i)
      near(math::length(vec(vertices[i].normal)), 1, 1e-6, "unit normal");
    std::size_t next = 0;
    for (std::size_t f = 0; f < nf; ++f) {
      const auto& face = faces[f];
      require(face.body_index < bodies.size() && face.first_index == next && face.index_count > 0 &&
                  face.index_count % 3 == 0 && next + face.index_count <= ni,
              "face range");
      const auto& id = *bodies[face.body_index].find("id")->if_string();
      const auto origin = original.bodies.at(id).shape->measures().centroid;
      for (std::size_t i = next; i < next + face.index_count; i += 3) {
        require(indices[i] < nv && indices[i + 1] < nv && indices[i + 2] < nv, "index bounds");
        const auto& va = vertices[indices[i]];
        const auto& vb = vertices[indices[i + 1]];
        const auto& vc = vertices[indices[i + 2]];
        const auto a = vec(va.position) - origin, b = vec(vb.position) - origin,
                   c = vec(vc.position) - origin;
        const auto cross = math::cross(b - a, c - a);
        require(math::length(cross) > 0 &&
                    math::dot(cross, vec(va.normal) + vec(vb.normal) + vec(vc.normal)) > 0,
                "nondegenerate outward winding");
        mesh_volumes[face.body_index] += math::dot(a, math::cross(b, c)) / 6;
        mesh_areas[face.body_index] += math::length(cross) / 2;
      }
      next += face.index_count;
    }
    require(next == ni, "complete index ranges");
    double max_volume_error = 0, max_area_error = 0, mesh_volume = 0, mesh_area = 0;
    for (std::size_t i = 0; i < bodies.size(); ++i) {
      const auto& id = *bodies[i].find("id")->if_string();
      const auto& m = original.bodies.at(id).shape->measures();
      const double ve = std::abs(mesh_volumes[i] / m.volume - 1),
                   ae = std::abs(mesh_areas[i] / m.area - 1);
      require(ve < .01 && ae < .01, "per-body mesh mass error " + id);
      max_volume_error = std::max(max_volume_error, ve);
      max_area_error = std::max(max_area_error, ae);
      mesh_volume += mesh_volumes[i];
      mesh_area += mesh_areas[i];
    }
    require(read(path) == source, "source file changed");
    const std::string receipt = ir::json::write(ir::json::Object{
        {"status", "passed"},
        {"body_metrics", std::move(metrics)},
        {"fixture_sha256", ir::sha256(source).hex()},
        {"probe_binary_sha256", ir::sha256(read(argv[0])).hex()},
        {"kernel", std::string(backend->identity())},
        {"step_sha256", ir::sha256(step).hex()},
        {"verification",
         ir::json::Object{
             {"per_body_analytic_volume_mm3", .02},
             {"symmetric_centroid_mm", 1e-6},
             {"foundation_analytic_area_mm2", .01},
             {"step_aggregate_comparison_mm3_mm2", .05},
             {"max_body_mesh_relative_error", .01},
             {"edit_restore_cache_clear", true},
             {"retained_snapshot", true},
             {"current_name_owners", true},
             {"complete_outward_mesh", true},
             {"separate_step_integrator", "OCCT BRepGProp 1e-9; same vendor, different integrator"},
             {"measurement_error_bounds", nullptr},
             {"physics", "not_run"},
             {"assembly_constraints", "not_run"},
             {"interactive_deadline", "not_run"}}},
        {"step_reader_volume_mm3", reader_volume.Mass()},
        {"step_reader_area_mm2", reader_area.Mass()},
        {"manifest_sha256", ir::sha256({json, json_bytes}).hex()},
        {"vertices_sha256",
         ir::sha256({reinterpret_cast<const char*>(vertices), nv * sizeof(*vertices)}).hex()},
        {"indices_sha256",
         ir::sha256({reinterpret_cast<const char*>(indices), ni * sizeof(*indices)}).hex()},
        {"faces_sha256",
         ir::sha256({reinterpret_cast<const char*>(faces), nf * sizeof(*faces)}).hex()},
        {"features", 113.},
        {"bodies", 32.},
        {"solids", static_cast<double>(exact.solids)},
        {"faces", static_cast<double>(exact.faces)},
        {"edges", static_cast<double>(exact.edges)},
        {"vertices", static_cast<double>(nv)},
        {"triangles", static_cast<double>(ni / 3)},
        {"volume_mm3", exact.volume},
        {"area_mm2", exact.area},
        {"cold_ms", cold_ms},
        {"bridge_ms", packet_ms},
        {"step_ms", step_ms},
        {"step_bytes", static_cast<double>(step.size())},
        {"mesh_volume_mm3", mesh_volume},
        {"mesh_area_mm2", mesh_area},
        {"max_body_mesh_volume_error", max_volume_error},
        {"max_body_mesh_area_error", max_area_error}});
    if (!folder.empty()) {
      std::filesystem::create_directories(folder);
      write(folder / "Eclipse.nightfall", source.data(), source.size());
      write(folder / "Eclipse.step", step.data(), step.size());
      write(folder / "manifest.json", json, json_bytes);
      write(folder / "vertices.bin", vertices, nv * sizeof(*vertices));
      write(folder / "indices.bin", indices, ni * sizeof(*indices));
      write(folder / "faces.bin", faces, nf * sizeof(*faces));
      write(folder / "receipt.json", receipt.data(), receipt.size());
    }
    std::puts(receipt.c_str());
  } catch (const std::exception& error) {
    failure.push_back({"error", std::string(error.what())});
    std::fprintf(stderr, "%s\n", ir::json::write(std::move(failure)).c_str());
    return 1;
  }
}
