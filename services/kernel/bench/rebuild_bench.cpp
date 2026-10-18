// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
// Real IR parse/validation/evaluation/kernel-validation benchmark. No IPC or tessellation.
#include "../tests/part_fixture.hpp"
#include "evaluator.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <stdexcept>

using namespace nf;
using namespace nf::kernel_service;

namespace {
constexpr int kSamples = 30;
struct Part {
  const char* name;
  ir::Document document;
  double area, center_x;
  std::size_t transforms = 28, cold_calls = 29, depth_calls = 29, depth_hits = 1;
  bool scale_profile_with_depth = false;
  double center_z_per_depth = 0.5;
  double (*surface_area)(double depth) = nullptr;
  std::size_t queries = 0;  // queries also execute when every feature is cached
};
void verify(const Evaluation& result, const Part& part, double depth, double tail,
            std::size_t calls, std::size_t hits) {
  for (const auto& feature : result.features)
    for (const auto& error : feature.errors)
      std::fprintf(stderr, "%s: %s\n", error.pointer.c_str(), error.message.c_str());
  if (!result.ok() || result.kernel_calls != calls || result.cache_hits != hits)
    throw std::runtime_error("unexpected evaluator status or execution count");
  const auto& m = result.bodies.at("plate").shape->measures();
  const auto translation = 3 * static_cast<double>(part.transforms - 1) + tail;
  const auto profile_scale = part.scale_profile_with_depth ? depth / 30 : 1;
  if (std::abs(m.volume - part.area * profile_scale * depth) > 1e-5 ||
      std::abs(m.centroid.x - (part.center_x + translation)) > 1e-6 ||
      std::abs(m.centroid.z - depth * part.center_z_per_depth) > 1e-6)
    throw std::runtime_error("analytic geometry verification failed");
  if (part.surface_area && std::abs(m.area - part.surface_area(depth)) > 1e-5)
    throw std::runtime_error("analytic surface area verification failed");
}
void measure(kernel::IKernel& backend, const Part& part, const std::string& mode) {
  // Explicit benchmark budget, not a proposed service-wide default.
  Evaluator evaluator(backend, {96, 64 * 1024 * 1024});
  auto document = part.document;
  auto warm = evaluator.evaluate(test::source(document)).value();
  verify(warm, part, 30, 3, part.cold_calls + part.queries, 0);
  std::vector<double> milliseconds;
  std::size_t calls = 0, hits = 0;
  for (int sample = 0; sample < kSamples; ++sample) {
    double depth = 30, tail = 3;
    if (mode == "cold") evaluator.clear_cache();
    if (mode == "depth edit") {
      depth = 31 + sample;
      document.params[0].value = std::to_string(depth) + " mm";
    }
    if (mode == "last transform edit") {
      tail = 4 + sample;
      document.params[3].value = std::to_string(tail) + " mm";
    }
    if (mode == "unused edit") document.params[2].value = static_cast<double>(sample + 2);
    const auto text = test::source(document);  // authoring/serialization outside timed rebuild
    const auto start = std::chrono::steady_clock::now();
    auto evaluation = evaluator.evaluate(text);
    const auto stop = std::chrono::steady_clock::now();
    if (!evaluation) throw std::runtime_error("document validation failed");
    calls = mode == "unused edit"           ? 0
            : mode == "last transform edit" ? 1
            : mode == "depth edit"          ? part.depth_calls
                                            : part.cold_calls;
    calls += part.queries;
    hits = mode == "cold"          ? 0
           : mode == "depth edit"  ? part.depth_hits
           : mode == "unused edit" ? 30
                                   : 29;
    verify(*evaluation, part, depth, tail, calls, hits);
    milliseconds.push_back(std::chrono::duration<double, std::milli>(stop - start).count());
  }
  std::ranges::sort(milliseconds);
  const auto usage = evaluator.cache_usage();
  std::printf(
      "%-12s %-19s median %7.3f ms  p95 %7.3f ms  max %7.3f ms  kernel %zu  hits %zu  cache %zu / "
      "%zu bytes\n",
      part.name, mode.c_str(), (milliseconds[14] + milliseconds[15]) / 2, milliseconds[28],
      milliseconds.back(), calls, hits, usage.entries, usage.estimated_bytes);
  std::fflush(stdout);
}
}  // namespace

int main() {
  try {
    auto backend = kernel::make_occt_kernel().value();
    auto plate = test::part(30);
    plate.params.push_back({"tail", "3 mm", {}, {}, {}, {}});
    plate.features.back().inputs =
        ir::json::parse(R"({"bodies":["plate"],"translation":["tail",0,0]})").value();
    auto linear = plate;
    linear.features.erase(linear.features.begin() + 2);  // 27 transforms: 30 features total
    linear.features.insert(
        linear.features.begin() + 2,
        test::feature("repeat", "pattern_linear", R"({"bodies":["plate"],"count":3,
          "spacing":15,"direction":"x"})"));
    Part linear_part{"linear pattern", linear, 600, 20, 27, 31, 31, 1};
    linear_part.surface_area = [](double depth) { return 1200 + 180 * depth; };
    auto circular = linear;
    circular.features[0].inputs = ir::json::parse(R"({"plane":{"datum":"xy"},"entities":[
      {"id":"r","type":"rect","corner":[9,-1],"size":[2,2]}],"constraints":[]})")
                                      .value();
    circular.features[2] = test::feature("repeat", "pattern_circular", R"({"bodies":["plate"],
      "count":4,"angle":360,"axis":{"origin":[0,0,0],"direction":"z"}})");
    Part circular_part{"circular pattern", circular, 16, 0, 27, 32, 32, 1};
    circular_part.surface_area = [](double depth) { return 32 + 32 * depth; };
    auto mirrored = circular;
    mirrored.features[2] =
        test::feature("reflect", "mirror", R"({"bodies":["plate"],"plane":{"datum":"yz"}})");
    Part mirror_part{"mirror", mirrored, 8, 0, 27, 30, 30, 1};
    mirror_part.surface_area = [](double depth) { return 16 + 16 * depth; };
    auto attached = plate;
    attached.features[1].inputs = ir::json::parse(R"({"profile":{"sketch":"outline"},
      "distance":"depth","result":{"new":"support","body_type":"brep"}})")
                                      .value();
    attached.features.erase(attached.features.begin() + 2, attached.features.begin() + 4);
    attached.features.insert(
        attached.features.begin() + 2,
        {test::feature("face_profile", "sketch", R"({"plane":{"created_by":"base","role":"cap_end",
        "expect":"one"},"entities":[{"id":"r","type":"rect","corner":[0,0],"size":[10,20]}],
        "constraints":[]})"),
         test::feature("cap", "extrude", R"({"profile":{"sketch":"face_profile"},"distance":"depth",
        "result":{"new":"plate","body_type":"brep"}})")});
    Part attached_part{"face attached", attached, 200, 5, 26, 28, 28, 1};
    attached_part.center_z_per_depth = 1.5;
    attached_part.surface_area = [](double depth) { return 400 + 60 * depth; };
    attached_part.queries = 1;
    auto filtered_part = attached_part;
    filtered_part.name = "filtered face";
    filtered_part.document.features[2] =
        test::feature("face_profile", "sketch",
                      R"({"plane":{"faces_of":"support","where":{"type":"plane","normal":"z",
        "parallel_to":"x","perpendicular_to":"z"},"expect":"one"},"entities":[
        {"id":"r","type":"rect","corner":[0,0],"size":[10,20]}],"constraints":[]})");
    auto nearest_part = filtered_part;
    nearest_part.name = "nearest face";
    auto& nearest = nearest_part.document.features;
    nearest.insert(
        nearest.begin() + 2,
        test::feature("repeat", "pattern_linear",
                      R"({"bodies":["support"],"count":2,"spacing":20,"direction":"x"})"));
    nearest.erase(nearest.begin() + 5);  // 30 features, including 25 transforms
    nearest[3] = test::feature(
        "face_profile", "sketch",
        R"({"plane":{"faces_of":"support","where":{"normal":"z","near":[25,10,"depth + 1 mm"]},
        "expect":"one"},"entities":[{"id":"r","type":"rect","corner":[0,0],"size":[10,20]}],
        "constraints":[]})");
    nearest_part.center_x = 25;
    nearest_part.transforms = 25;
    nearest_part.cold_calls = nearest_part.depth_calls = 29;
    nearest_part.queries = 3;  // enumeration plus distances to both separated top caps
    auto composed_part = nearest_part;
    composed_part.name = "selector sets";
    composed_part.document.features[3] =
        test::feature("face_profile", "sketch",
                      R"({"plane":{"any":[{"created_by":"base","role":"cap_end"},
        {"faces_of":"support","where":{"normal":"z"}}],"except":{"created_by":"base","role":"cap_end",
        "where":{"near":[5,10,"depth + 1 mm"]},"expect":"one"},
        "where":{"normal":"z","near":[5,10,"depth + 1 mm"]},"expect":"one"},
        "entities":[{"id":"r","type":"rect","corner":[0,0],"size":[10,20]}],"constraints":[]})");
    composed_part.queries = 4;  // one shared enumeration; two exclusion and one remaining distance
    auto bored = plate;
    bored.features[0].inputs = ir::json::parse(R"({"plane":{"datum":"xy"},"entities":[
      {"id":"r","type":"rect","corner":[0,0],"size":[40,20]},
      {"id":"h1","type":"circle","center":[10,10],"radius":2},
      {"id":"h2","type":"circle","center":[30,10],"radius":2}],"constraints":[]})")
                                   .value();
    auto sleeve = plate;
    sleeve.features[0].inputs = ir::json::parse(R"({"plane":{"datum":"zx"},"entities":[
      {"id":"r","type":"rect","corner":[0,5],"size":["depth",5]}],"constraints":[]})")
                                    .value();
    sleeve.features[1] = test::feature("base", "revolve", R"({"profile":{"sketch":"outline"},
      "axis":{"origin":[0,0,0],"direction":"z"},"angle":360,
      "result":{"new":"plate","body_type":"brep"}})");
    auto sketch_axis = sleeve;
    sketch_axis.features[0].inputs = ir::json::parse(R"({"plane":{"datum":"zx"},"entities":[
      {"id":"a","type":"line","start":[0,5],"end":["depth",5]},
      {"id":"b","type":"line","start":["depth",5],"end":["depth",10]},
      {"id":"c","type":"line","start":["depth",10],"end":[0,10]},
      {"id":"d","type":"line","start":[0,5],"end":[0,10]}],"constraints":[]})")
                                         .value();
    sketch_axis.features[1] = test::feature("base", "revolve", R"({"profile":{"sketch":"outline"},
      "axis":{"sketch":"outline","entity":"a"},"angle":360,
      "result":{"new":"plate","body_type":"brep"}})");
    auto open_axis = sleeve;
    open_axis.features.erase(open_axis.features.begin() + 2);  // 27 transforms: 30 features total
    open_axis.features.insert(
        open_axis.features.begin() + 1,
        test::feature("axis_sketch", "sketch", R"({"plane":{"datum":"zx"},"entities":[
          {"id":"axis","type":"line","start":[0,0],"end":["depth",0]}],
          "constraints":[{"type":"horizontal","entities":["axis"]}]})"));
    open_axis.features[2] = test::feature("base", "revolve", R"({"profile":{"sketch":"outline"},
      "axis":{"sketch":"axis_sketch","entity":"axis"},"angle":360,
      "result":{"new":"plate","body_type":"brep"}})");
    auto torus = sleeve;
    torus.features[0].inputs = ir::json::parse(R"({"plane":{"datum":"zx"},"entities":[
      {"id":"section","type":"circle","center":[0,20],"radius":"depth / 10"}],
      "constraints":[]})")
                                   .value();
    auto elliptical = plate;
    elliptical.features[0].inputs = ir::json::parse(R"({"plane":{"datum":"xy"},"entities":[
      {"id":"outer","type":"ellipse","center":[0,0],"radii":["depth / 3",5],"rotation":35},
      {"id":"inner","type":"ellipse","center":[0,0],"radii":["depth / 6",2.5],"rotation":35}],
      "constraints":[]})")
                                        .value();
    auto shared = plate;
    shared.features[0].inputs = ir::json::parse(R"({"plane":{"datum":"xy"},"entities":[
      {"id":"left","type":"rect","corner":["-depth / 3",0],"size":["depth / 3",20]},
      {"id":"right","type":"rect","corner":[0,0],"size":["depth / 3",20]}],
      "constraints":[]})")
                                    .value();
    auto shared_arc = plate;
    shared_arc.features[0].inputs = ir::json::parse(R"({"plane":{"datum":"xy"},"entities":[
      {"id":"outer","type":"circle","center":[0,0],"radius":5},
      {"id":"cap","type":"arc","center":[0,0],"radius":5,"start_angle":0,"sweep_angle":90},
      {"id":"chord","type":"line","start":[0,5],"end":[5,0]}],"constraints":[]})")
                                        .value();
    const double shared_arc_area = 25 * (3 * std::numbers::pi / 4 + 0.5);
    auto constrained = plate;
    constrained.features[0].inputs = ir::json::parse(R"({"plane":{"datum":"xy"},"entities":[
      {"id":"a","type":"line","start":["-depth / 6",-10],"end":["depth / 6",-10]},
      {"id":"b","type":"line","start":["depth / 6",-10],"end":["depth / 6",10]},
      {"id":"c","type":"line","start":["depth / 6",10],"end":["-depth / 6",10]},
      {"id":"d","type":"line","start":["-depth / 6",10],"end":["-depth / 6",-10]}],
      "constraints":[{"type":"horizontal","entities":["a"]},
      {"type":"vertical","entities":["b"]},{"type":"parallel","entities":["a","c"]},
      {"type":"perpendicular","entities":["a","b"]},{"type":"equal","entities":["a","c"]},
      {"type":"equal","entities":["b","d"]}]})")
                                         .value();
    auto point_constrained = constrained;
    auto point_entities = *constrained.features[0].inputs.find("entities")->if_array();
    const auto points = ir::json::parse(R"([
      {"id":"m","type":"point","at":[0,-10]},
      {"id":"p","type":"point","at":["-depth / 6 - 2 mm",0]},
      {"id":"q","type":"point","at":["-depth / 6 + 2 mm",0]}])")
                            .value();
    point_entities.insert(point_entities.end(), points.if_array()->begin(),
                          points.if_array()->end());
    auto point_constraints = *constrained.features[0].inputs.find("constraints")->if_array();
    const auto relations = ir::json::parse(R"([
      {"type":"coincident","entities":[{"entity":"a","point":"end"},{"entity":"b","point":"start"}]},
      {"type":"distance","entities":[{"entity":"a","point":"start"},{"entity":"a","point":"end"}],"value":"depth / 3"},
      {"type":"midpoint","entities":["m","a"]},{"type":"symmetric","entities":["p","q","d"]}])")
                               .value();
    point_constraints.insert(point_constraints.end(), relations.if_array()->begin(),
                             relations.if_array()->end());
    point_constrained.features[0].inputs =
        ir::json::Object{{"plane", *constrained.features[0].inputs.find("plane")},
                         {"entities", std::move(point_entities)},
                         {"constraints", std::move(point_constraints)}};
    auto rounded = plate;
    rounded.params.push_back({"r", "sqrt(depth / 30 mm) * 5 mm", {}, {}, {}, {}});
    rounded.features[0].inputs = ir::json::parse(R"({"plane":{"datum":"xy"},"entities":[
      {"id":"a","type":"line","start":["-2 * r","-r"],"end":["2 * r","-r"]},
      {"id":"b","type":"arc","center":["2 * r",0],"radius":"r","start_angle":-90,"sweep_angle":180},
      {"id":"c","type":"line","start":["2 * r","r"],"end":["-2 * r","r"]},
      {"id":"d","type":"arc","center":["-2 * r",0],"radius":"r","start_angle":90,"sweep_angle":180}],
      "constraints":[{"type":"tangent","entities":["a","b"]},{"type":"tangent","entities":["b","c"]},
      {"type":"tangent","entities":["c","d"]},{"type":"tangent","entities":["d","a"]}]})")
                                     .value();
    auto angled = plate;
    angled.params.push_back({"theta", "depth / 1 mm * 1 deg", {}, {}, {}, {}});
    angled.features[0].inputs = ir::json::parse(R"json({"plane":{"datum":"xy"},"entities":[
      {"id":"a","type":"line","start":["-5 mm - 10 mm / tan(theta)",-10],"end":["5 mm - 10 mm / tan(theta)",-10]},
      {"id":"b","type":"line","start":["5 mm - 10 mm / tan(theta)",-10],"end":["5 mm + 10 mm / tan(theta)",10]},
      {"id":"c","type":"line","start":["5 mm + 10 mm / tan(theta)",10],"end":["-5 mm + 10 mm / tan(theta)",10]},
      {"id":"d","type":"line","start":["-5 mm + 10 mm / tan(theta)",10],"end":["-5 mm - 10 mm / tan(theta)",-10]}],
      "constraints":[{"type":"angle","entities":["a","b"],"value":"theta"},
        {"type":"fixed","entities":["a"]},{"type":"fixed","entities":[{"entity":"b","point":"end"}]}]})json")
                                    .value();
    Part angle_part{"angle fixed", angled, 200, 0, 28, 29, 29, 0};
    angle_part.surface_area = [](double depth) {
      return 400 + depth * (20 + 40 / std::sin(depth * std::numbers::pi / 180));
    };
    auto cut = plate;
    cut.features.erase(cut.features.begin() + 2, cut.features.begin() + 5);
    cut.features.insert(cut.features.begin() + 2,
                        {test::feature("hole_profile", "sketch", R"({"plane":{"origin":[0,0,-5],
        "normal":[0,0,1],"x_direction":[1,0,0]},"entities":[
        {"id":"c","type":"circle","center":[5,10],"radius":2}],"constraints":[]})"),
                         test::feature("tool", "extrude", R"({"profile":{"sketch":"hole_profile"},
        "distance":100,"result":{"new":"drill","body_type":"brep"}})"),
                         test::feature("cut", "boolean",
                                       R"({"op":"subtract","target":"plate","tools":["drill"]})")});
    std::puts(
        "30-feature real B-rep rebuilds; 30 samples per case; parse/validation/evaluation "
        "included.");
    std::puts(
        "Unique edits; analytic volume/centroid and execution counts checked for every sample. No "
        "IPC/tessellation.");
    for (const auto& part :
         {Part{"plate", plate, 200, 5},
          linear_part,
          circular_part,
          mirror_part,
          attached_part,
          filtered_part,
          nearest_part,
          composed_part,
          Part{"bored plate", bored, 800 - 8 * std::numbers::pi, 20},
          Part{"boolean cut", cut, 200 - 4 * std::numbers::pi, 5, 25, 28, 27, 3},
          Part{"sleeve", sleeve, 75 * std::numbers::pi, 0, 28, 29, 29, 0},
          Part{"sketch axis", sketch_axis, 25 * std::numbers::pi, 5, 28, 29, 29, 0},
          Part{"open axis", open_axis, 75 * std::numbers::pi, 0, 27, 28, 28, 0},
          Part{"elliptic tube", elliptical, 37.5 * std::numbers::pi, 0, 28, 29, 29, 0, true},
          Part{"shared edge", shared, 400, 0, 28, 29, 29, 0, true},
          Part{"shared arc", shared_arc, shared_arc_area, -125 / (6 * shared_arc_area)},
          Part{"constrained", constrained, 200, 0, 28, 29, 29, 0, true},
          Part{"point refs", point_constrained, 200, 0, 28, 29, 29, 0, true},
          Part{"tangent", rounded, 25 * (8 + std::numbers::pi), 0, 28, 29, 29, 0, true},
          angle_part,
          Part{"toroidal seal", torus, 12 * std::numbers::pi * std::numbers::pi, 0, 28, 29, 29, 0,
               true, 0}})
      for (const auto& mode : {"cold", "depth edit", "last transform edit", "unused edit"})
        measure(*backend, part, mode);
  } catch (const std::exception& error) {
    std::fprintf(stderr, "benchmark failed: %s\n", error.what());
    return 1;
  }
}
