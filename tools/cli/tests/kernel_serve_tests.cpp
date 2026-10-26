// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// End-to-end checks of `nf kernel serve` (RFC 0015) through a spawned process. Operands are made
// and results read back with the kernel in this process; the client's own face and edge numbers
// are deliberately unlike the kernel's, so history must come from the sample-point mapping.
#include "nf/ir/json.hpp"
#include "nf/kernel/kernel.hpp"
#include "nf/math/constructors.hpp"

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numbers>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

extern char** environ;
using namespace nf;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
namespace fs = std::filesystem;
using ir::json::Array;
using ir::json::Object;
using ir::json::Value;

namespace {
std::string slurp(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  REQUIRE(stream.is_open());
  return {std::istreambuf_iterator<char>{stream}, {}};
}
/// Runs `nf kernel serve` over the given stdin lines; one parsed reply per stdout line.
std::vector<Value> serve(const std::vector<Value>& requests, std::string raw = {}) {
  auto pattern = (fs::temp_directory_path() / "nightfall-serve-test-XXXXXX").string();
  REQUIRE(mkdtemp(pattern.data()));
  const fs::path dir = pattern;
  {
    std::ofstream in(dir / "stdin", std::ios::binary);
    for (const auto& r : requests) in << ir::json::canonical(r) << '\n';
    in << raw;
  }
  std::vector<std::string> args{NF_CLI_PATH, "kernel", "serve"};
  std::vector<char*> argv;
  for (auto& arg : args) argv.push_back(arg.data());
  argv.push_back(nullptr);
  posix_spawn_file_actions_t actions;
  REQUIRE(posix_spawn_file_actions_init(&actions) == 0);
  REQUIRE(posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, (dir / "stdin").c_str(),
                                           O_RDONLY, 0) == 0);
  REQUIRE(posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, (dir / "stdout").c_str(),
                                           O_WRONLY | O_CREAT | O_TRUNC, 0600) == 0);
  REQUIRE(posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, (dir / "stderr").c_str(),
                                           O_WRONLY | O_CREAT | O_TRUNC, 0600) == 0);
  pid_t pid = 0;
  const auto spawned = posix_spawn(&pid, argv.front(), &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  REQUIRE(spawned == 0);
  int status = 0;
  pid_t waited;
  do {
    waited = waitpid(pid, &status, 0);
  } while (waited < 0 && errno == EINTR);
  REQUIRE(waited == pid);
  REQUIRE(WIFEXITED(status));
  CHECK(WEXITSTATUS(status) == 0);
  std::vector<Value> replies;
  std::istringstream out(slurp(dir / "stdout"));
  for (std::string line; std::getline(out, line);) {
    const auto value = ir::json::parse(line);
    if (!value) WARN(line);
    REQUIRE(value);
    replies.push_back(*value);
  }
  std::error_code ignored;
  fs::remove_all(dir, ignored);
  return replies;
}
Value point(math::Vec3 p) { return Array{p.x, p.y, p.z}; }
Array points(std::initializer_list<math::Vec3> list) {
  Array result;
  for (const auto& p : list) result.push_back(point(p));
  return result;
}
kernel::ShapePtr prism(kernel::IKernel& kernel, std::vector<math::NurbsCurve2> loop,
                       kernel::Plane plane, math::Vec3 sweep) {
  const std::vector<std::vector<math::NurbsCurve2>> paths{std::move(loop)};
  return kernel.extrude({plane, math::regions(paths, math::FillRule::kEvenOdd).value(), sweep})
      .value()
      .shape;
}
/// A 40 x 30 x 10 block at the origin, with client faces and edges in the client's own order.
Object block(kernel::IKernel& kernel) {
  using math::make_line;
  using math::Vec2;
  const auto shape = prism(kernel,
                           {make_line(Vec2{}, {40, 0}), make_line(Vec2{40, 0}, {40, 30}),
                            make_line(Vec2{40, 30}, {0, 30}), make_line(Vec2{0, 30}, {})},
                           {}, {0, 0, 10});
  return {{"step", kernel.export_step({shape}).value()},
          {"faces",
           points({{20, 15, 10}, {0, 15, 5}, {20, 15, 0}, {40, 15, 5}, {20, 30, 5}, {20, 0, 5}})},
          {"edges", points({{20, 0, 10}, {40, 15, 10}, {0, 0, 5}})}};
}
/// A bore through the block, split into two half faces on the client as truck does.
Object bore(kernel::IKernel& kernel) {
  const double h = 6 / std::numbers::sqrt2;  // off the kernel's arc joints at quarter turns
  const auto shape = prism(kernel, {math::make_circle({20, 15}, 6)}, {{0, 0, -5}}, {0, 0, 20});
  return {
      {"step", kernel.export_step({shape}).value()},
      {"faces", points({{20, 15, 15}, {20 + h, 15 + h, 5}, {20 - h, 15 - h, 5}, {20, 15, -5}})}};
}
const Value& ok(const Value& reply) {
  INFO(ir::json::canonical(reply));
  REQUIRE(reply.find("ok"));
  REQUIRE(*reply.find("ok")->if_bool());
  return reply;
}
std::string error_code(const Value& reply) {
  INFO(ir::json::canonical(reply));
  REQUIRE_FALSE(*reply.find("ok")->if_bool());
  return *reply.find("error")->find("code")->if_string();
}
/// The result solid, its kernel faces, and the reply's face entries in the same order.
struct Result {
  kernel::ShapePtr shape;
  std::vector<kernel::FaceInfo> faces;
  const Array* entries;
};
Result result(kernel::IKernel& kernel, const Value& reply) {
  const auto solids = kernel.import_step(*ok(reply).find("step")->if_string()).value();
  REQUIRE(solids.size() == 1);
  Result r{solids.front(), kernel.faces(solids.front()).value(), reply.find("faces")->if_array()};
  REQUIRE(r.entries->size() == r.faces.size());
  for (std::size_t i = 0; i < r.faces.size(); ++i)
    CHECK_THAT(*(*r.entries)[i].find("area")->if_number(),
               WithinRel(kernel.face_mass(r.faces[i].entity).value().area, 1e-9));
  return r;
}
std::string from(const Value& entry) {
  std::string text = *entry.find("change")->if_string() + ":";
  for (const auto& f : *entry.find("from")->if_array())
    text += " " + std::to_string(*f.find("operand")->if_integer()) +
            (f.find("face") ? "f" + std::to_string(*f.find("face")->if_integer())
                            : "e" + std::to_string(*f.find("edge")->if_integer()));
  return text;
}
}  // namespace

TEST_CASE("Kernel serve says hello with its protocol and kernel", "[kernel-serve]") {
  const auto replies = serve({Object{{"id", 1.}, {"op", "hello"}}});
  REQUIRE(replies.size() == 1);
  CHECK(*ok(replies[0]).find("protocol")->if_integer() == 1);
  CHECK(replies[0].find("kernel")->if_string()->starts_with("occt/7.9.3"));
  CHECK(*replies[0].find("id")->if_integer() == 1);
}

TEST_CASE("Kernel serve subtracts with history in the client's face numbers", "[kernel-serve]") {
  auto kernel = kernel::make_occt_kernel().value();
  const auto replies = serve({Object{{"id", 2.},
                                     {"op", "boolean"},
                                     {"kind", "subtract"},
                                     {"operands", Array{block(*kernel), bore(*kernel)}}}});
  REQUIRE(replies.size() == 1);
  const auto r = result(*kernel, replies[0]);
  CHECK_THAT(r.shape->measures().volume, WithinRel(12000 - 360 * std::numbers::pi, 1e-9));
  std::vector<std::string> names;
  for (const auto& entry : *r.entries) names.push_back(from(entry));
  std::ranges::sort(names);
  // The kernel's bore is four quarter faces, unified into one by the boolean.
  CHECK(names == std::vector<std::string>{"kept: 0f1", "kept: 0f3", "kept: 0f4", "kept: 0f5",
                                          "merged: 1f1 1f2", "modified: 0f0", "modified: 0f2"});
}

TEST_CASE("Kernel serve merges an unchanged face the client had split", "[kernel-serve]") {
  auto kernel = kernel::make_occt_kernel().value();
  // The client's x = 0 side is two faces (1 and 6) on one kernel face, untouched by the bore.
  auto split = block(*kernel);
  for (auto& m : split)
    if (m.key == "faces")
      m.value = points(
          {{20, 15, 10}, {0, 15, 5}, {20, 15, 0}, {40, 15, 5}, {20, 30, 5}, {20, 0, 5}, {0, 5, 5}});
  const auto replies = serve({Object{{"id", 2.},
                                     {"op", "boolean"},
                                     {"kind", "subtract"},
                                     {"operands", Array{split, bore(*kernel)}}}});
  REQUIRE(replies.size() == 1);
  const auto r = result(*kernel, replies[0]);
  CHECK(std::ranges::count_if(*r.entries,
                              [](const Value& e) { return from(e) == "merged: 0f1 0f6"; }) == 1);
}

TEST_CASE("Kernel serve fillets and chamfers the client's edges", "[kernel-serve]") {
  auto kernel = kernel::make_occt_kernel().value();
  const auto replies = serve({Object{{"id", 3.},
                                     {"op", "fillet"},
                                     {"operands", Array{block(*kernel)}},
                                     {"edges", Array{0., 1.}},
                                     {"radius", 2.}},
                              Object{{"id", 4.},
                                     {"op", "chamfer"},
                                     {"operands", Array{block(*kernel)}},
                                     {"edges", Array{2.}},
                                     {"distance", 1.}}});
  REQUIRE(replies.size() == 2);
  const auto fillet = result(*kernel, replies[0]);
  // Two rounded edges (40 and 30 long) and the corner where they meet.
  CHECK(fillet.shape->measures().volume < 12000 - (4 - std::numbers::pi) * 66);
  CHECK(fillet.shape->measures().volume > 12000 - (4 - std::numbers::pi) * 70);
  std::vector<std::string> made;
  for (const auto& entry : *fillet.entries)
    if (entry.find("role")) {
      CHECK(*entry.find("role")->if_string() == "fillet");
      made.push_back(from(entry));
    }
  std::ranges::sort(made);
  CHECK(made == std::vector<std::string>{"generated: 0e0", "generated: 0e1"});
  const auto chamfer = result(*kernel, replies[1]);
  CHECK_THAT(chamfer.shape->measures().volume, WithinRel(12000 - 0.5 * 10, 1e-9));
  CHECK(std::ranges::count_if(*chamfer.entries, [](const Value& e) {
          return from(e) == "generated: 0e2" && *e.find("role")->if_string() == "chamfer";
        }) == 1);
}

TEST_CASE("Kernel serve takes a body in several pieces as one operand", "[kernel-serve]") {
  auto kernel = kernel::make_occt_kernel().value();
  using math::make_line;
  using math::Vec2;
  const auto cube = prism(*kernel,
                          {make_line(Vec2{}, {10, 0}), make_line(Vec2{10, 0}, {10, 10}),
                           make_line(Vec2{10, 10}, {0, 10}), make_line(Vec2{0, 10}, {})},
                          {}, {0, 0, 10});
  const auto apart = kernel->transform(cube, {{30, 0, 0}, {}, {0, 0, 1}, 0}).value().shape;
  const Object pieces{{"step", kernel->export_step({cube, apart}).value()},
                      {"faces", points({{5, 5, 10}, {35, 5, 10}})}};
  const auto slab = prism(*kernel,
                          {make_line(Vec2{-5, -5}, {50, -5}), make_line(Vec2{50, -5}, {50, 15}),
                           make_line(Vec2{50, 15}, {-5, 15}), make_line(Vec2{-5, 15}, {-5, -5})},
                          {{0, 0, 4}}, {0, 0, 2});
  const Object tool{{"step", kernel->export_step({slab}).value()}, {"faces", Array{}}};
  const auto replies = serve({Object{
      {"id", 10.}, {"op", "boolean"}, {"kind", "subtract"}, {"operands", Array{pieces, tool}}}});
  REQUIRE(replies.size() == 1);
  const auto solids = kernel->import_step(*ok(replies[0]).find("step")->if_string()).value();
  CHECK(solids.size() == 4);
  double volume = 0;
  for (const auto& s : solids) volume += s->measures().volume;
  CHECK_THAT(volume, WithinRel(2 * 1000 * 0.8, 1e-9));
  // Both tops survive, each from its own client face; the bottoms, which the client gave no
  // samples for, survive with no client origin.
  std::vector<std::string> kept;
  for (const auto& e : *replies[0].find("faces")->if_array())
    if (*e.find("change")->if_string() == "kept") kept.push_back(from(e));
  std::ranges::sort(kept);
  CHECK(kept == std::vector<std::string>{"kept:", "kept:", "kept: 0f0", "kept: 0f1"});
}

TEST_CASE("Kernel serve rejects bad requests and keeps serving", "[kernel-serve]") {
  auto kernel = kernel::make_occt_kernel().value();
  auto corner = block(*kernel);
  for (auto& m : corner)
    if (m.key == "faces") m.value = points({{0, 0, 0}});
  auto stray = block(*kernel);
  for (auto& m : stray)
    if (m.key == "edges") m.value = points({{20, 15, 5}});
  // An edge sample on a vertex is ambiguous even when that edge is not selected.
  auto vertex = block(*kernel);
  for (auto& m : vertex)
    if (m.key == "edges") m.value = points({{20, 0, 10}, {40, 30, 10}});
  const auto replies =
      serve({Object{{"id", 5.},
                    {"op", "fillet"},
                    {"operands", Array{corner}},
                    {"edges", Array{0.}},
                    {"radius", 1.}},
             Object{{"id", 6.},
                    {"op", "fillet"},
                    {"operands", Array{stray}},
                    {"edges", Array{0.}},
                    {"radius", 1.}},
             Object{{"id", 7.},
                    {"op", "boolean"},
                    {"kind", "subtract"},
                    {"operands", Array{Object{{"step", "not step"}, {"faces", Array{}}}}}},
             Object{{"id", 8.}, {"op", "twist"}},
             Object{{"id", 10.},
                    {"op", "fillet"},
                    {"operands", Array{vertex}},
                    {"edges", Array{0.}},
                    {"radius", 1.}}},
            "{nope\n{\"id\":9,\"op\":\"hello\"}\n");
  REQUIRE(replies.size() == 7);
  CHECK(error_code(replies[0]) == "unmatched_face");
  CHECK(error_code(replies[1]) == "unmatched_edge");
  CHECK(error_code(replies[2]) == "step_read");
  // An unknown op is unsupported before its operands are looked at (it has none).
  CHECK(error_code(replies[3]) == "unsupported");
  CHECK(error_code(replies[4]) == "unmatched_edge");
  CHECK(error_code(replies[5]) == "bad_request");
  CHECK(replies[5].find("id")->is_null());
  CHECK(*ok(replies[6]).find("id")->if_integer() == 9);
}
