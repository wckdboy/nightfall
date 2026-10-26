// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "../../../services/kernel/tests/part_fixture.hpp"
#include "evaluator.hpp"
#include "snapshot.hpp"

#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Reader.hxx>

#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numbers>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

extern char** environ;
using namespace nf;
using namespace nf::kernel_service;
using namespace nf::kernel_service::test;
using Catch::Matchers::WithinAbs;
namespace fs = std::filesystem;

namespace {
std::string read(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  REQUIRE(stream.is_open());
  return {std::istreambuf_iterator<char>{stream}, {}};
}
void write(const fs::path& path, std::string_view text) {
  std::ofstream stream(path, std::ios::binary);
  REQUIRE(stream.is_open());
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
  REQUIRE(stream.good());
}
struct Temp {
  fs::path directory;
  Temp() {
    auto pattern = (fs::temp_directory_path() / "nightfall-cli-XXXXXX").string();
    REQUIRE(mkdtemp(pattern.data()));
    directory = pattern;
  }
  ~Temp() {
    std::error_code ignored;
    fs::remove_all(directory, ignored);
  }
  fs::path path(const char* name) const { return directory / name; }
  void no_temporary_outputs() const {
    for (const auto& item : fs::directory_iterator(directory))
      CHECK_FALSE(item.path().filename().string().starts_with(".nightfall-"));
  }
};
struct Process {
  int status;
  std::string output, error;
};
Process run(const Temp& temp, std::vector<std::string> args) {
  args.insert(args.begin(), NF_CLI_PATH);
  std::vector<char*> argv;
  for (auto& arg : args) argv.push_back(arg.data());
  argv.push_back(nullptr);
  posix_spawn_file_actions_t actions;
  REQUIRE(posix_spawn_file_actions_init(&actions) == 0);
  REQUIRE(posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, temp.path("stdout").c_str(),
                                           O_WRONLY | O_CREAT | O_TRUNC, 0600) == 0);
  REQUIRE(posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, temp.path("stderr").c_str(),
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
  return {WEXITSTATUS(status), read(temp.path("stdout")), read(temp.path("stderr"))};
}
ir::json::Value json(std::string_view text) {
  const auto value = ir::json::parse(text);
  if (!value) WARN(text);
  REQUIRE(value);
  return *value;
}
void error(const Process& process, const char* code, const char* pointer = "") {
  INFO(process.error);
  REQUIRE(process.status != 0);
  const auto value = json(process.error);
  REQUIRE(value.find("errors"));
  const auto& errors = *value.find("errors")->if_array();
  REQUIRE_FALSE(errors.empty());
  CHECK(*errors.front().find("code")->if_string() == code);
  CHECK(*errors.front().find("pointer")->if_string() == pointer);
  CHECK(process.output.empty());
}
kernel::ShapePtr reload(const fs::path& path) {
  CHECK(read(path).starts_with("ISO-10303-21;"));
  STEPControl_Reader reader;
  REQUIRE(reader.ReadFile(path.c_str()) == IFSelect_RetDone);
  reader.SetSystemLengthUnit(1);
  REQUIRE(reader.TransferRoots() > 0);
  const auto shape = kernel::detail::snapshot(reader.OneShape());
  REQUIRE(shape);
  return *shape;
}
void measures(const kernel::ShapePtr& shape, double volume, double area, math::Vec3 center,
              std::size_t solids = 1) {
  const auto& m = shape->measures();
  CHECK_THAT(m.volume, WithinAbs(volume, 1e-5));
  CHECK_THAT(m.area, WithinAbs(area, 1e-5));
  CHECK_THAT(m.centroid.x, WithinAbs(center.x, 1e-6));
  CHECK_THAT(m.centroid.y, WithinAbs(center.y, 1e-6));
  CHECK_THAT(m.centroid.z, WithinAbs(center.z, 1e-6));
  CHECK(m.solids == solids);
}
}  // namespace

TEST_CASE("build command publishes exact STEP and a machine readable success record",
          "[cli-build]") {
  Temp temp;
  const auto input = temp.path("input $model; 'quote'.nightfall");
  const auto output = temp.path("result with spaces.step");
  const auto text = source(part());
  write(input, text);
  const auto result = run(temp, {"build", input.string(), "--out", output.string()});
  INFO(result.error);
  REQUIRE(result.status == 0);
  CHECK(result.error.empty());
  const auto value = json(result.output);
  REQUIRE(value.find("bodies"));
  CHECK(*value.find("bodies")->if_array() == ir::json::Array{"plate"});
  CHECK(*value.find("output")->if_string() == output.string());
  measures(reload(output), 6000, 2200, {8, 10, 15});
  CHECK(read(input) == text);
  temp.no_temporary_outputs();
}

TEST_CASE("chamfer parameter variants export and reload as exact closed STEP solids",
          "[cli-build][cli-chamfer]") {
  Temp temp;
  auto d = part(2);
  d.params.push_back({"bevel", "1 mm", {}, {}, {}, {}});
  d.features.push_back(feature("bevel_top", "chamfer", R"({"distance":"bevel",
    "edges":{"edges_of":{"created_by":"base","role":"cap_end","expect":"one"}}})"));
  const auto text = source(d);
  const auto input = temp.path("chamfer.nightfall");
  const auto output = temp.path("chamfer.step");
  write(input, text);
  for (const double bevel : {1., 2.}) {
    const auto process = run(temp, {"build", input.string(), "--out", output.string(), "--set",
                                    "bevel=" + std::to_string(bevel) + " mm"});
    INFO(process.error);
    REQUIRE(process.status == 0);
    const double removed = 30 * bevel * bevel - 4 * std::pow(bevel, 3) / 3;
    const double volume = 6000 - removed;
    const double area =
        2200 - 120 * bevel + 4 * bevel * bevel + (60 * bevel - 4 * bevel * bevel) * std::sqrt(2.);
    const double moment = 90000 - 30 * removed + 10 * std::pow(bevel, 3) - std::pow(bevel, 4) / 3;
    const auto shape = reload(output);
    measures(shape, volume, area, {5, 10, moment / volume});
    CHECK(shape->measures().faces == 10);
    CHECK(shape->measures().edges == 20);
    CHECK(read(output).find("TESSELLATED") == std::string::npos);
    CHECK(read(input) == text);
    temp.no_temporary_outputs();
  }
}

TEST_CASE("build command validates syntax and offers help without opening a model", "[cli-build]") {
  Temp temp;
  const auto help = run(temp, {"--help"});
  CHECK(help.status == 0);
  CHECK(help.output.find("nf build") != std::string::npos);
  CHECK(help.error.empty());
  for (const auto& args : {std::vector<std::string>{},
                           {"build"},
                           {"serve"},
                           {"build", "missing", "--out"},
                           {"build", "missing", "--bad", "x"},
                           {"build", "missing", "--out", "x", "extra"}}) {
    const auto result = run(temp, args);
    error(result, "usage");
    CHECK(result.status == 2);
  }
}

TEST_CASE("build command bounds reads and reports file and document errors", "[cli-build]") {
  Temp temp;
  const auto input = temp.path("input.nightfall");
  const auto output = temp.path("output.step");
  SECTION("missing file") {
    error(run(temp, {"build", input.string(), "--out", output.string()}), "io");
  }
  SECTION("directory is not a model file") {
    error(run(temp, {"build", temp.directory.string(), "--out", output.string()}), "io");
  }
  SECTION("malformed document") {
    write(input, "{");
    error(run(temp, {"build", input.string(), "--out", output.string()}), "parse");
  }
  SECTION("oversized file") {
    write(input, std::string(ir::kMaxDocumentBytes + 1, ' '));
    error(run(temp, {"build", input.string(), "--out", output.string()}), "parse");
  }
  CHECK_FALSE(fs::exists(output));
  temp.no_temporary_outputs();
}

TEST_CASE("failed builds preserve old output and do not export independent successful bodies",
          "[cli-build]") {
  Temp temp;
  auto d = part();
  d.features.push_back(feature("bad", "extrude", R"({"profile":{"sketch":"outline"},
    "distance":0,"result":{"new":"bad_body","body_type":"brep"}})"));
  const auto input = temp.path("input.nightfall"), output = temp.path("old.step");
  write(input, source(d));
  write(output, "previous artifact");
  error(run(temp, {"build", input.string(), "--out", output.string()}), "kernel",
        "/features/3/inputs");
  CHECK(read(output) == "previous artifact");
  temp.no_temporary_outputs();
}

TEST_CASE("build command preserves dimensional diagnostics and rejects parts without solids",
          "[cli-build]") {
  Temp temp;
  auto d = part(2);
  const auto input = temp.path("input.nightfall"), output = temp.path("output.step");
  SECTION("unit failure") {
    d.features[1] = feature("base", "extrude", R"({"profile":{"sketch":"outline"},
      "distance":"5 deg","result":{"new":"plate","body_type":"brep"}})");
    write(input, source(d));
    error(run(temp, {"build", input.string(), "--out", output.string()}), "unit",
          "/features/1/inputs/distance");
  }
  SECTION("only a sketch") {
    d.features.resize(1);
    write(input, source(d));
    error(run(temp, {"build", input.string(), "--out", output.string()}), "kernel");
  }
  CHECK_FALSE(fs::exists(output));
}

TEST_CASE("build command refuses input aliases as output paths", "[cli-build]") {
  Temp temp;
  const auto input = temp.path("input.nightfall"), output = temp.path("alias.step");
  const auto text = source(part());
  write(input, text);
  SECTION("same path") {
    error(run(temp, {"build", input.string(), "--out", input.string()}), "io");
  }
  SECTION("hard link") {
    fs::create_hard_link(input, output);
    error(run(temp, {"build", input.string(), "--out", output.string()}), "io");
    CHECK(read(output) == text);
  }
  SECTION("symbolic link") {
    fs::create_symlink(input, output);
    error(run(temp, {"build", input.string(), "--out", output.string()}), "io");
    CHECK(read(output) == text);
  }
  CHECK(read(input) == text);
  temp.no_temporary_outputs();
}

TEST_CASE("build command handles publication failures and replaces outputs only after success",
          "[cli-build]") {
  Temp temp;
  const auto input = temp.path("input.nightfall");
  write(input, source(part()));
  SECTION("missing parent") {
    const auto output = temp.path("missing/output.step");
    error(run(temp, {"build", input.string(), "--out", output.string()}), "io");
    CHECK_FALSE(fs::exists(output));
  }
  SECTION("output directory") {
    error(run(temp, {"build", input.string(), "--out", temp.directory.string()}), "io");
    CHECK(fs::is_directory(temp.directory));
  }
  SECTION("replace an artifact") {
    const auto output = temp.path("output.step");
    write(output, "previous artifact");
    const auto result = run(temp, {"build", input.string(), "--out", output.string()});
    REQUIRE(result.status == 0);
    measures(reload(output), 6000, 2200, {8, 10, 15});
  }
  temp.no_temporary_outputs();
}

TEST_CASE("build command exports every live body once and excludes consumed tools", "[cli-build]") {
  Temp temp;
  auto d = part(2);
  d.features.push_back(feature("second", "extrude", R"({"profile":{"sketch":"outline"},
    "distance":"depth","result":{"new":"other","body_type":"brep"}})"));
  double volume = 12000, area = 4400;
  std::size_t solids = 2;
  ir::json::Array bodies{"other", "plate"};
  SECTION("coincident bodies sharing a cached snapshot") {}
  SECTION("consumed boolean tool") {
    d.features.push_back(
        feature("fuse", "boolean", R"({"target":"plate","tools":["other"],"op":"union"})"));
    volume = 6000;
    area = 2200;
    solids = 1;
    bodies = {"plate"};
  }
  const auto input = temp.path("input.nightfall"), output = temp.path("output.step");
  write(input, source(d));
  const auto result = run(temp, {"build", input.string(), "--out", output.string()});
  REQUIRE(result.status == 0);
  const auto value = json(result.output);
  CHECK(*value.find("bodies")->if_array() == bodies);
  measures(reload(output), volume, area, {5, 10, 15}, solids);
}

TEST_CASE("build command respects suppression and inclusive history rollback", "[cli-build]") {
  Temp temp;
  auto d = part();
  SECTION("suppression") { d.features[2].suppressed = true; }
  SECTION("rollback") { d.history_end = "base"; }
  const auto input = temp.path("input.nightfall"), output = temp.path("output.step");
  write(input, source(d));
  const auto result = run(temp, {"build", input.string(), "--out", output.string()});
  REQUIRE(result.status == 0);
  measures(reload(output), 6000, 2200, {5, 10, 15});
}

TEST_CASE("all twenty reference parts build through the CLI and reload as valid exact solids",
          "[cli-build][cli-reference]") {
  Temp temp;
  std::size_t count = 0;
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  for (const auto& entry : fs::directory_iterator(NF_REFERENCE_PARTS_DIR)) {
    if (entry.path().extension() != ".nightfall") continue;
    CAPTURE(entry.path());
    ++count;
    const auto output = temp.path("reference.step");
    const auto result = run(temp, {"build", entry.path().string(), "--out", output.string()});
    INFO(result.error);
    REQUIRE(result.status == 0);
    CHECK(result.error.empty());
    const auto value = json(result.output);
    CHECK(*value.find("bodies")->if_array() == ir::json::Array{"part"});
    const auto original = evaluator.evaluate(read(entry.path())).value();
    REQUIRE(original.ok());
    const auto exported = reload(output);
    const auto& m = original.bodies.at("part").shape->measures();
    measures(exported, m.volume, m.area, m.centroid);
  }
  CHECK(count == 20);
  temp.no_temporary_outputs();
}

TEST_CASE("parameter overrides export variants and never persist into their source",
          "[cli-overrides]") {
  Temp temp;
  const auto input = temp.path("input.nightfall"), output = temp.path("variant.step");
  const auto text = source(part());
  write(input, text);
  for (const auto* expression : {"45 mm", "4.5 cm"}) {
    const auto result = run(temp, {"build", input.string(), "--out", output.string(), "--set",
                                   "depth=" + std::string(expression)});
    INFO(result.error);
    REQUIRE(result.status == 0);
    CHECK(result.error.empty());
    const auto value = json(result.output);
    CHECK(*value.find("output")->if_string() == output.string());
    CHECK(*value.find("bodies")->if_array() == ir::json::Array{"plate"});
    measures(reload(output), 9000, 3100, {8, 10, 22.5});
    CHECK(read(input) == text);
  }
  const auto original = run(temp, {"build", input.string(), "--out", output.string()});
  REQUIRE(original.status == 0);
  measures(reload(output), 6000, 2200, {8, 10, 15});
  CHECK(read(input) == text);
  temp.no_temporary_outputs();
}

TEST_CASE("override batches validate the final dependency graph regardless of flag order",
          "[cli-overrides]") {
  Temp temp;
  auto d = part();
  d.params[1].value = "depth / 10";
  const auto text = source(d);
  const auto input = temp.path("input.nightfall"), output = temp.path("variant.step");
  write(input, text);
  // Updating depth alone would create a cycle with the source's step expression.
  for (const bool reverse : {false, true}) {
    const auto first = reverse ? "step=4 mm" : "depth=step * 10";
    const auto second = reverse ? "depth=step * 10" : "step=4 mm";
    const auto result = run(
        temp, {"build", input.string(), "--set", first, "--out", output.string(), "--set", second});
    INFO(result.error);
    REQUIRE(result.status == 0);
    measures(reload(output), 8000, 2800, {9, 10, 20});
    CHECK(read(input) == text);
  }
  temp.no_temporary_outputs();
}

TEST_CASE("scalar parameter overrides remain dimensionless for body pattern counts",
          "[cli-overrides]") {
  Temp temp;
  auto d = part(2);
  d.params.push_back({"instances", 2.0, 1.0, 4.0, {}, {}});
  d.features.push_back(
      feature("repeat", "pattern_linear",
              R"({"bodies":["plate"],"count":"instances","spacing":15,"direction":"x"})"));
  const auto input = temp.path("input.nightfall"), output = temp.path("variant.step");
  const auto text = source(d);
  write(input, text);
  const auto result =
      run(temp, {"build", input.string(), "--set", "instances=3", "--out", output.string()});
  INFO(result.error);
  REQUIRE(result.status == 0);
  measures(reload(output), 18000, 6600, {20, 10, 15}, 3);
  CHECK(read(input) == text);
}

TEST_CASE("parameter override bounds are inclusive and do not clamp invalid variants",
          "[cli-overrides]") {
  Temp temp;
  auto d = part();
  d.params[0].min = "20 mm";
  d.params[0].max = "40 mm";
  const auto input = temp.path("input.nightfall"), output = temp.path("variant.step");
  const auto text = source(d);
  write(input, text);
  for (const int depth : {20, 40}) {
    const auto result = run(temp, {"build", input.string(), "--out", output.string(), "--set",
                                   "depth=" + std::to_string(depth) + " mm"});
    REQUIRE(result.status == 0);
    measures(reload(output), 200.0 * depth, 400 + 60.0 * depth, {8, 10, depth / 2.0});
  }
  const auto previous = read(output);
  error(run(temp, {"build", input.string(), "--out", output.string(), "--set", "depth=50 mm"}),
        "bounds", "/params/0/value");
  CHECK(read(output) == previous);
  CHECK(read(input) == text);
  temp.no_temporary_outputs();
}

TEST_CASE("invalid override expressions preserve parameter diagnostics and old artifacts",
          "[cli-overrides]") {
  Temp temp;
  const auto input = temp.path("input.nightfall"), output = temp.path("variant.step");
  const auto text = source(part());
  write(input, text);
  write(output, "previous artifact");
  struct Invalid {
    const char* expression;
    const char* code;
    const char* pointer;
  };
  for (const auto& [expression, code, pointer] :
       {Invalid{"1 mm + 2 deg", "unit", "/params/0/value"},
        Invalid{"missing + 1 mm", "unknown_id", "/params/0/value"},
        Invalid{"depth", "cycle", "/params/0/value"},
        Invalid{"sqrt(-1) * 1 mm", "bounds", "/params/0/value"},
        Invalid{"1 / 0 * 1 mm", "bounds", "/params/0/value"},
        Invalid{"2 mm +", "parse", "/params/0/value"}, Invalid{" ", "parse", "/params/0/value"},
        Invalid{"30", "unit", "/features/1/inputs/distance"}}) {
    INFO(expression);
    const auto result = run(temp, {"build", input.string(), "--out", output.string(), "--set",
                                   "depth=" + std::string(expression)});
    error(result, code, pointer);
    CHECK(result.status == 1);
    if (std::string_view(expression) == "2 mm +") {
      const auto value = json(result.error);
      const auto& first = value.find("errors")->if_array()->front();
      REQUIRE(first.find("offset"));
      CHECK(*first.find("offset")->if_number() >= 0);
      CHECK(*first.find("offset")->if_number() <= 6);
    }
    CHECK(read(output) == "previous artifact");
    CHECK(read(input) == text);
    temp.no_temporary_outputs();
  }
}

TEST_CASE("parameter overrides reject unknown names without adding source parameters",
          "[cli-overrides]") {
  Temp temp;
  const auto input = temp.path("input.nightfall"), output = temp.path("variant.step");
  const auto text = source(part());
  write(input, text);
  write(output, "previous artifact");
  const auto result = run(temp, {"build", input.string(), "--set", "new_parameter=10 mm", "--set",
                                 "depth=40 mm", "--out", output.string()});
  error(result, "unknown_id", "/params");
  CHECK(result.status == 1);
  CHECK(result.error.find("new_parameter") != std::string::npos);
  CHECK(read(input) == text);
  CHECK(read(output) == "previous artifact");
  temp.no_temporary_outputs();
}

TEST_CASE("override syntax and duplicate flags fail as usage errors before opening input",
          "[cli-overrides]") {
  Temp temp;
  const auto input = temp.path("missing.nightfall"), output = temp.path("variant.step");
  const std::vector<std::vector<std::string>> invalid{
      {"--set"},
      {"--set", "depth"},
      {"--set", "=3 mm"},
      {"--set", "depth="},
      {"--set", "Depth=3 mm"},
      {"--set", "1depth=3 mm"},
      {"--set", "bad/name=3 mm"},
      {"--set", "depth =3 mm"},
      {"--set", std::string(65, 'a') + "=3 mm"},
      {"--set", "depth=3 mm", "--set", "depth=4 mm"},
      {"--out", output.string()},
      {"--set=depth=3 mm"},
      {"--unknown", "3 mm"}};
  for (const auto& flags : invalid) {
    std::vector<std::string> args{"build", input.string(), "--out", output.string()};
    args.insert(args.end(), flags.begin(), flags.end());
    const auto result = run(temp, args);
    error(result, "usage");
    CHECK(result.status == 2);
  }
  CHECK_FALSE(fs::exists(output));
  temp.no_temporary_outputs();
}

TEST_CASE("override parameter IDs accept the schema length boundary and underscore prefix",
          "[cli-overrides]") {
  Temp temp;
  for (const auto& name : {std::string("_"), std::string(63, 'a') + "0"}) {
    auto d = part();
    d.params[2].name = name;  // unused scalar
    const auto input = temp.path("input.nightfall"), output = temp.path("variant.step");
    const auto text = source(d);
    write(input, text);
    const auto result =
        run(temp, {"build", input.string(), "--set", name + "=2", "--out", output.string()});
    INFO(result.error);
    REQUIRE(result.status == 0);
    measures(reload(output), 6000, 2200, {8, 10, 15});
    CHECK(read(input) == text);
  }
}

TEST_CASE("parameter overrides require a valid source and cannot bypass input alias protection",
          "[cli-overrides]") {
  Temp temp;
  const auto input = temp.path("input.nightfall"), output = temp.path("variant.step");
  SECTION("invalid source bounds") {
    auto d = part();
    d.params[0].max = "40 mm";
    auto text = source(d);
    const auto at = text.find("30 mm");
    REQUIRE(at != std::string::npos);
    text.replace(at, 5, "50 mm");
    write(input, text);
    error(run(temp, {"build", input.string(), "--set", "depth=30 mm", "--out", output.string()}),
          "bounds", "/params/0/value");
    CHECK(read(input) == text);
    CHECK_FALSE(fs::exists(output));
  }
  SECTION("input alias") {
    const auto text = source(part());
    write(input, text);
    fs::create_hard_link(input, output);
    error(run(temp, {"build", input.string(), "--set", "depth=40 mm", "--out", output.string()}),
          "io");
    CHECK(read(input) == text);
    CHECK(read(output) == text);
  }
  temp.no_temporary_outputs();
}

TEST_CASE("all twenty reference parts export parameter variants without source edits",
          "[cli-overrides][cli-reference]") {
  Temp temp;
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {128, 64 * 1024 * 1024});
  // The first edited values also have independent analytic checks in reference_part_tests.cpp.
  for (const auto& [name, expression] : {std::pair{"mounting_plate", "2 mm"},
                                         {"washer", "1 mm"},
                                         {"hex_spacer", "6 mm"},
                                         {"angle_bracket", "20 mm"},
                                         {"channel", "25 mm"},
                                         {"enclosure", "12 mm"},
                                         {"stepped_shaft", "20 mm"},
                                         {"flanged_bushing", "10 mm"},
                                         {"conical_nozzle", "12 mm"},
                                         {"spherical_knob", "7 mm"},
                                         {"toroidal_seal", "2 mm"},
                                         {"semicircular_saddle", "4 mm"},
                                         {"slotted_link", "3 mm"},
                                         {"elliptic_duct", "10 mm"},
                                         {"spline_wedge", "2 mm"},
                                         {"gusset", "2 mm"},
                                         {"offset_foot", "3 mm"},
                                         {"twin_boss_plate", "5 mm"},
                                         {"drilled_block", "8 mm"},
                                         {"intersected_rail", "6 mm"}}) {
    CAPTURE(name);
    const auto input = fs::path(NF_REFERENCE_PARTS_DIR) / (std::string(name) + ".nightfall");
    const auto original = read(input);
    auto document = ir::read_document(original).value();
    document.params.front().value = expression;
    const auto output = temp.path("variant.step");
    const auto result =
        run(temp, {"build", input.string(), "--set",
                   document.params.front().name + "=" + expression, "--out", output.string()});
    INFO(result.error);
    REQUIRE(result.status == 0);
    CHECK(result.error.empty());
    const auto expected = evaluator.evaluate(source(document)).value();
    REQUIRE(expected.ok());
    const auto& m = expected.bodies.at("part").shape->measures();
    measures(reload(output), m.volume, m.area, m.centroid);
    CHECK(read(input) == original);
  }
  temp.no_temporary_outputs();
}

TEST_CASE("filleted plate exports exact parameter variants through CLI without source edits",
          "[cli-fillet]") {
  Temp temp;
  const auto input =
      fs::path{NF_REFERENCE_PARTS_DIR}.parent_path() / "modeling/rounded_plate.nightfall";
  const auto original = read(input);
  const auto output = temp.path("rounded.step");
  const auto first = run(temp, {"build", input.string(), "--out", output.string()});
  INFO(first.error);
  REQUIRE(first.status == 0);
  constexpr double pi = std::numbers::pi;
  measures(reload(output), 18048, 5728 + 288 * pi, {0, 0, 4});
  const auto variant =
      run(temp, {"build", input.string(), "--set", "corner_radius=3 mm", "--out", output.string()});
  INFO(variant.error);
  REQUIRE(variant.status == 0);
  measures(reload(output), 18912 - 216 * pi, 6136 + 186 * pi, {0, 0, 4});
  const auto saved = read(output);
  CHECK(run(temp,
            {"build", input.string(), "--set", "corner_radius=100 mm", "--out", output.string()})
            .status != 0);
  CHECK(read(output) == saved);
  CHECK(read(input) == original);
  temp.no_temporary_outputs();
}

TEST_CASE(
    "build evidence identifies effective geometry and exact exported "
    "bytes at full precision",
    "[cli-build][cli-evidence]") {
  Temp temp;
  auto document = part();
  const auto text = source(document);
  const auto input = temp.path("evidence.nightfall");
  const auto output = temp.path("evidence.step");
  write(input, text);
  const std::string depth = "30.1234567890123 mm";
  const auto process =
      run(temp, {"build", input.string(), "--out", output.string(), "--set", "depth=" + depth});
  INFO(process.error);
  REQUIRE(process.status == 0);
  const auto value = json(process.output);
  CHECK(ir::json::canonical(value) + "\n" == process.output);
  const auto* evidence = value.find("evidence");
  REQUIRE(evidence);
  CHECK(*evidence->find("input_sha256")->if_string() == ir::sha256(text).hex());
  document.params[0].value = depth;
  CHECK(*evidence->find("effective_source_sha256")->if_string() ==
        ir::sha256(source(document)).hex());
  CHECK(*evidence->find("input_sha256") != *evidence->find("effective_source_sha256"));
  const ir::json::Object request{{"input_sha256", ir::sha256(text).hex()},
                                 {"overrides", ir::json::Object{{"depth", depth}}}};
  CHECK(*evidence->find("request_sha256")->if_string() ==
        ir::sha256(ir::json::canonical(request)).hex());
  auto kernel = kernel::make_occt_kernel().value();
  CHECK(*evidence->find("kernel")->if_string() == kernel->identity());
  const auto& units = *evidence->find("units");
  CHECK(*units.find("length")->if_string() == "mm");
  CHECK(*units.find("area")->if_string() == "mm^2");
  CHECK(*units.find("volume")->if_string() == "mm^3");
  const auto& tolerances = *evidence->find("comparison_tolerances");
  CHECK(*tolerances.find("linear_mm")->if_number() == math::Tolerance::standard().linear);
  CHECK(*tolerances.find("angular_rad")->if_number() == math::Tolerance::standard().angular);
  const auto& checks = *evidence->find("checks");
  CHECK(*checks.find("closed_solids")->if_string() == "kernel_validated");
  CHECK(*checks.find("step_roundtrip")->if_string() == "not_run");
  CHECK(*checks.find("physics")->if_string() == "not_run");
  REQUIRE(evidence->find("measurement_error_bounds")->is_null());
  const auto& metrics = *evidence->find("body_metrics")->if_array();
  REQUIRE(metrics.size() == 1);
  const auto& body = metrics.front();
  CHECK(*body.find("id")->if_string() == "plate");
  const double volume = *body.find("volume")->if_number();
  CHECK_THAT(volume, WithinAbs(200 * 30.1234567890123, 1e-8));
  CHECK(volume != std::round(volume * 1e6) / 1e6);
  CHECK_THAT(*body.find("area")->if_number(), WithinAbs(400 + 60 * 30.1234567890123, 1e-8));
  const auto& center = *body.find("centroid")->if_array();
  CHECK_THAT(*center[0].if_number(), WithinAbs(8, 1e-8));
  CHECK_THAT(*center[1].if_number(), WithinAbs(10, 1e-8));
  CHECK_THAT(*center[2].if_number(), WithinAbs(30.1234567890123 / 2, 1e-8));
  CHECK(*body.find("solids")->if_integer() == 1);
  CHECK(*body.find("faces")->if_integer() == 6);
  CHECK(*body.find("edges")->if_integer() == 12);
  const auto& minimum = *body.find("minimum")->if_array();
  CHECK_THAT(*minimum[0].if_number(), WithinAbs(3, 1e-8));
  CHECK_THAT(*minimum[1].if_number(), WithinAbs(0, 1e-8));
  CHECK_THAT(*minimum[2].if_number(), WithinAbs(0, 1e-8));
  CHECK_THAT(*body.find("maximum")->if_array()->at(2).if_number(),
             WithinAbs(30.1234567890123, 1e-8));
  Evaluator evaluator(*kernel, {96, 64 * 1024 * 1024});
  const auto evaluated = evaluator.evaluate(source(document)).value();
  REQUIRE(evaluated.ok());
  CHECK(*body.find("state_hash")->if_string() == evaluated.bodies.at("plate").hash->hex());
  CHECK(*body.find("name_records")->if_integer() ==
        static_cast<std::int64_t>(evaluated.bodies.at("plate").names.size()));
  const auto& cache = *evidence->find("cache");
  CHECK(*cache.find("kernel_calls")->if_integer() == 2);
  CHECK(*cache.find("hits")->if_integer() == 0);
  CHECK(*cache.find("binding_misses")->if_integer() == 0);
  const auto& statuses = *evidence->find("feature_status_counts");
  CHECK(*statuses.find("built")->if_integer() == 3);
  CHECK(*statuses.find("failed")->if_integer() == 0);
  const auto& timings = *evidence->find("timing_ms");
  CHECK(*timings.find("evaluation")->if_number() >= 0);
  CHECK(*timings.find("step_export")->if_number() >= 0);
  const auto bytes = read(output);
  const auto& artifact = *evidence->find("step");
  CHECK(*artifact.find("sha256")->if_string() == ir::sha256(bytes).hex());
  CHECK(*artifact.find("bytes")->if_integer() == static_cast<std::int64_t>(bytes.size()));
  measures(reload(output), 200 * 30.1234567890123, 400 + 60 * 30.1234567890123,
           {8, 10, 30.1234567890123 / 2});
  CHECK(read(input) == text);
  temp.no_temporary_outputs();
}

TEST_CASE(
    "rejected build evidence identifies its source without stale "
    "geometry or artifact claims",
    "[cli-build][cli-evidence]") {
  Temp temp;
  const auto input = temp.path("invalid.nightfall");
  const auto output = temp.path("preserved.step");
  write(input, "{}");
  write(output, "prior artifact");
  const auto process = run(temp, {"build", input.string(), "--out", output.string()});
  REQUIRE(process.status == 1);  // a build failure, not a usage error
  CHECK(process.output.empty());
  const auto value = json(process.error);
  REQUIRE_FALSE(value.find("errors")->if_array()->empty());
  const auto* evidence = value.find("evidence");
  REQUIRE(evidence);
  CHECK(*evidence->find("input_sha256")->if_string() == ir::sha256("{}").hex());
  CHECK(*evidence->find("effective_source_sha256")->if_string() == ir::sha256("{}").hex());
  CHECK_FALSE(evidence->find("body_metrics"));
  CHECK_FALSE(evidence->find("step"));
  CHECK_FALSE(evidence->find("checks"));
  CHECK(read(output) == "prior artifact");
  temp.no_temporary_outputs();
}

TEST_CASE("partial evaluation and publication failures never emit success-only evidence",
          "[cli-build][cli-evidence]") {
  for (const bool publication_error : {false, true}) {
    INFO(publication_error);
    Temp temp;
    auto document = part();
    if (!publication_error) {
      document.features[2].inputs =
          ir::json::parse(R"({"bodies":["plate"],"translation":["unknown_length",0,0]})").value();
      document.features.push_back(feature("independent", "extrude",
                                          R"({"profile":{"sketch":"outline"},"distance":5,
            "result":{"new":"other","body_type":"brep"}})"));
    }
    const auto text = source(document);
    const auto input = temp.path("input.nightfall");
    const auto output = temp.path("prior.step");
    write(input, text);
    if (publication_error)
      REQUIRE(fs::create_directory(output));
    else
      write(output, "prior artifact");
    const auto process = run(temp, {"build", input.string(), "--out", output.string()});
    REQUIRE(process.status == 1);  // a build failure, not a usage error
    CHECK(process.output.empty());
    const auto value = json(process.error);
    REQUIRE_FALSE(value.find("errors")->if_array()->empty());
    const auto* evidence = value.find("evidence");
    REQUIRE(evidence);
    CHECK(*evidence->find("input_sha256")->if_string() == ir::sha256(text).hex());
    CHECK(*evidence->find("feature_status_counts")->find("failed")->if_integer() ==
          (publication_error ? 0 : 1));
    CHECK_FALSE(evidence->find("body_metrics"));
    CHECK_FALSE(evidence->find("step"));
    CHECK_FALSE(evidence->find("checks"));
    if (publication_error)
      CHECK(fs::is_directory(output));
    else
      CHECK(read(output) == "prior artifact");
    temp.no_temporary_outputs();
  }
}
