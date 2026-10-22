// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// End-to-end checks of `nf mcp` through a spawned process: one JSON-RPC message per stdin line,
// replies read back from stdout. Geometry is checked by measured values, never by status alone.
#include "../../../services/kernel/tests/part_fixture.hpp"
#include "evaluator.hpp"
#include "nf/ir/patch.hpp"
#include "snapshot.hpp"

#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Reader.hxx>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <numbers>
#include <set>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

extern char** environ;
using namespace nf;
using namespace nf::kernel_service::test;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
namespace fs = std::filesystem;
using ir::json::Array;
using ir::json::Object;
using ir::json::Value;

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
    auto pattern = (fs::temp_directory_path() / "nightfall-mcp-test-XXXXXX").string();
    REQUIRE(mkdtemp(pattern.data()));
    directory = fs::canonical(pattern);
  }
  ~Temp() {
    std::error_code ignored;
    fs::remove_all(directory, ignored);
  }
  fs::path path(const char* name) const { return directory / name; }
};
struct Process {
  int status;
  std::vector<Value> replies;  // one per stdout line
  std::string error;
};
/// Runs `nf mcp args...` with the given stdin lines and parses every stdout line as JSON.
Process serve(const Temp& temp, std::vector<std::string> args,
              const std::vector<std::string>& lines) {
  std::string input;
  for (const auto& line : lines) input += line + '\n';
  write(temp.path("stdin"), input);
  args.insert(args.begin(), "mcp");
  args.insert(args.begin(), NF_CLI_PATH);
  std::vector<char*> argv;
  for (auto& arg : args) argv.push_back(arg.data());
  argv.push_back(nullptr);
  posix_spawn_file_actions_t actions;
  REQUIRE(posix_spawn_file_actions_init(&actions) == 0);
  REQUIRE(posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, temp.path("stdin").c_str(),
                                           O_RDONLY, 0) == 0);
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
  Process process{WEXITSTATUS(status), {}, read(temp.path("stderr"))};
  std::istringstream out(read(temp.path("stdout")));
  for (std::string line; std::getline(out, line);) {
    const auto value = ir::json::parse(line);
    if (!value) WARN(line);
    REQUIRE(value);
    process.replies.push_back(*value);
  }
  return process;
}
std::string request(int id, const char* method, const char* params = "{}") {
  return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) + ",\"method\":\"" + method +
         "\",\"params\":" + params + "}";
}
std::string call(int id, const char* tool, std::string arguments = "{}") {
  // One message per line: arguments written across lines in a test are flattened.
  std::erase(arguments, '\n');
  return request(
      id, "tools/call",
      ("{\"name\":\"" + std::string(tool) + "\",\"arguments\":" + arguments + "}").c_str());
}
const Value& result(const Value& reply) {
  INFO(ir::json::canonical(reply));
  REQUIRE(reply.find("result"));
  return *reply.find("result");
}
/// The structured content of a tool result; checks the isError flag.
const Value& content(const Value& reply, bool is_error = false) {
  INFO(ir::json::canonical(reply));
  const auto& r = result(reply);
  REQUIRE(r.find("structuredContent"));
  const auto* flag = r.find("isError");
  CHECK((flag != nullptr) == is_error);
  // The text block repeats the structured content canonically.
  const auto& text = *r.find("content")->if_array()->front().find("text")->if_string();
  CHECK(text == ir::json::canonical(*r.find("structuredContent")));
  return *r.find("structuredContent");
}
double number(const Value& v, const char* key) {
  REQUIRE(v.find(key));
  REQUIRE(v.find(key)->if_number());
  return *v.find(key)->if_number();
}
const Value& body(const Value& build, const char* id) {
  for (const auto& b : *build.find("bodies")->if_array())
    if (*b.find("id")->if_string() == id) return b;
  FAIL("no body " << id);
  return build;
}
Array ids(const Value& v, const char* key) {
  REQUIRE(v.find(key));
  REQUIRE(v.find(key)->if_array());
  return *v.find(key)->if_array();
}
kernel::ShapePtr reload(const fs::path& path) {
  CHECK(read(path).starts_with("ISO-10303-21;"));
  STEPControl_Reader reader;
  REQUIRE(reader.ReadFile(path.c_str()) == IFSelect_RetDone);
  reader.SetSystemLengthUnit(1);
  REQUIRE(reader.TransferRoots() > 0);
  return *kernel::detail::snapshot(reader.OneShape());
}
const char* kInitialize =
    R"({"jsonrpc":"2.0","id":0,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"0"}}})";
}  // namespace

TEST_CASE("mcp server negotiates, lists typed tools and inspects a part", "[cli-mcp]") {
  Temp temp;
  const auto input = temp.path("plate.nightfall");
  write(input, source(part()));
  const auto process = serve(
      temp, {"--in", input.string()},
      {kInitialize, R"({"jsonrpc":"2.0","method":"notifications/initialized"})", request(1, "ping"),
       request(2, "tools/list"), call(3, "inspect_part"), call(4, "get_schema")});
  INFO(process.error);
  REQUIRE(process.status == 0);
  REQUIRE(process.replies.size() == 5);  // the notification gets no reply
  const auto& init = result(process.replies[0]);
  CHECK(*init.find("protocolVersion")->if_string() == "2025-03-26");
  CHECK(init.find("capabilities")->find("tools"));
  CHECK(*process.replies[0].find("id")->if_number() == 0);
  CHECK(result(process.replies[1]).if_object()->empty());
  std::vector<std::string> names;
  for (const auto& tool : *result(process.replies[2]).find("tools")->if_array()) {
    names.push_back(*tool.find("name")->if_string());
    REQUIRE(tool.find("inputSchema"));
    CHECK(*tool.find("inputSchema")->find("type")->if_string() == "object");
    CHECK(tool.find("annotations")->find("readOnlyHint")->if_bool());
  }
  CHECK(names == std::vector<std::string>{"inspect_part",
                                          "get_schema",
                                          "get_part",
                                          "body_topology",
                                          "measure",
                                          "set_parameters",
                                          "replace_part",
                                          "patch_part",
                                          "inspect_assembly",
                                          "drive_joints",
                                          "check_interference",
                                          "measure_clearance",
                                          "mass_properties",
                                          "export_urdf",
                                          "list_studies",
                                          "run_study",
                                          "study_results",
                                          "sweep_joint",
                                          "undo",
                                          "redo",
                                          "open_part",
                                          "save_part",
                                          "export_step",
                                          "export_mesh"});
  const auto& inspected = content(process.replies[3]);
  CHECK(*inspected.find("document_sha256")->if_string() == ir::sha256(source(part())).hex());
  CHECK(*inspected.find("ok")->if_bool());
  CHECK(ids(inspected, "params").size() == 3);
  const auto& features = ids(inspected, "features");
  REQUIRE(features.size() == 3);
  CHECK(*features[1].find("type")->if_string() == "extrude");
  CHECK(*features[1].find("status")->if_string() == "built");
  CHECK(ids(features[1], "errors").empty());
  const auto& plate = body(inspected, "plate");
  CHECK_THAT(number(plate, "volume"), WithinAbs(6000, 1e-6));
  CHECK_THAT(number(plate, "area"), WithinAbs(2200, 1e-6));
  CHECK(number(plate, "faces") == 6);
  const auto& schema = content(process.replies[4]);
  CHECK(schema.find("schema")->find("$defs"));
}

TEST_CASE("mcp parameter edits rebuild with body deltas and roll back failures", "[cli-mcp]") {
  Temp temp;
  const auto input = temp.path("plate.nightfall");
  const auto text = source(part());
  write(input, text);
  const auto process =
      serve(temp, {"--in", input.string()},
            {kInitialize, call(1, "set_parameters", R"({"values":{"depth":"60 mm"}})"),
             call(2, "set_parameters", R"({"values":{"depth":"not a length"}})"),
             call(3, "set_parameters", R"({"values":{"missing":"1 mm"}})"), call(4, "measure"),
             call(5, "set_parameters", R"({"values":{"depth":"2 * step"}})"),
             call(6, "measure", R"({"body":"plate"})"), call(7, "undo"), call(8, "undo"),
             call(9, "undo"), call(10, "redo"), call(11, "measure", R"({"body":"nothing"})"),
             call(12, "set_parameters", R"({"values":{"depth":3}})"),
             call(13, "save_part", R"({"path":"saved.nightfall"})")});
  INFO(process.error);
  REQUIRE(process.status == 0);
  REQUIRE(process.replies.size() == 14);
  const auto& doubled = content(process.replies[1]);
  CHECK(*doubled.find("ok")->if_bool());
  CHECK(ids(doubled, "changed_bodies") == Array{"plate"});
  CHECK(ids(doubled, "added_bodies").empty());
  CHECK_THAT(number(body(doubled, "plate"), "volume"), WithinAbs(12000, 1e-6));
  CHECK(*doubled.find("evidence")->find("checks")->find("closed_solids")->if_string() ==
        "kernel_validated");
  // A bad expression names its field and leaves the document unchanged.
  const auto& bad = content(process.replies[2], true);
  const auto& errors = ids(bad, "errors");
  REQUIRE_FALSE(errors.empty());
  CHECK(*errors.front().find("pointer")->if_string() == "/params/0/value");
  CHECK(*ids(content(process.replies[3], true), "errors").front().find("code")->if_string() ==
        "unknown_id");
  const auto& measured = content(process.replies[4]);
  CHECK_THAT(number(*measured.find("totals"), "volume"), WithinAbs(12000, 1e-6));
  // Expressions in terms of other parameters: depth = 2 * step = 6 mm.
  CHECK_THAT(number(body(content(process.replies[5]), "plate"), "volume"), WithinAbs(1200, 1e-6));
  CHECK_THAT(number(content(process.replies[6]), "volume"), WithinAbs(1200, 1e-6));
  CHECK_THAT(number(body(content(process.replies[7]), "plate"), "volume"), WithinAbs(12000, 1e-6));
  CHECK_THAT(number(body(content(process.replies[8]), "plate"), "volume"), WithinAbs(6000, 1e-6));
  CHECK(*ids(content(process.replies[9], true), "errors").front().find("code")->if_string() ==
        "history");
  CHECK_THAT(number(body(content(process.replies[10]), "plate"), "volume"), WithinAbs(12000, 1e-6));
  CHECK(*ids(content(process.replies[11], true), "errors").front().find("code")->if_string() ==
        "unknown_id");
  // A JSON number is a dimensionless parameter value; the extrude then needs a length.
  CHECK(content(process.replies[12], true).find("errors"));
  const auto& saved = content(process.replies[13]);
  CHECK(*saved.find("path")->if_string() == temp.path("saved.nightfall").string());
  const auto written = read(temp.path("saved.nightfall"));
  CHECK(*saved.find("sha256")->if_string() == ir::sha256(written).hex());
  auto expected = part();
  expected.params.front().value = "60 mm";
  CHECK(written == source(expected));
  CHECK(read(input) == text);
}

TEST_CASE("mcp replaces documents, opens and exports within allowed folders only", "[cli-mcp]") {
  Temp temp;
  Temp elsewhere;
  const auto input = temp.path("plate.nightfall");
  write(input, source(part()));
  auto bevelled = part(2);
  bevelled.params.push_back({"bevel", "1 mm", {}, {}, {}, {}});
  bevelled.features.push_back(feature("bevel_top", "chamfer", R"({"distance":"bevel",
    "edges":{"edges_of":{"created_by":"base","role":"cap_end","expect":"one"}}})"));
  write(elsewhere.path("other.nightfall"), source(bevelled));
  write(temp.path(".hidden.nightfall"), source(bevelled));
  auto broken = part();
  broken.features[1].inputs = ir::json::parse(R"({"profile":{"sketch":"outline"},
    "distance":"depth","result":{"new":"plate","body_type":"mesh"}})")
                                  .value();
  const auto escaped = [](std::string s) { return ir::json::canonical(Value(std::move(s))); };
  const auto process = serve(
      temp, {"--in", input.string()},
      {kInitialize, call(1, "replace_part", "{\"document\":" + escaped(source(broken)) + "}"),
       call(2, "replace_part", "{\"document\":" + escaped(source(broken)) + ",\"rollback\":false}"),
       call(3, "inspect_part"), call(4, "export_step", R"({"path":"broken.step"})"),
       call(5, "undo"),
       call(6, "open_part",
            "{\"path\":" + escaped(elsewhere.path("other.nightfall").string()) + "}"),
       call(7, "open_part", R"({"path":".hidden.nightfall"})"),
       call(8, "open_part", R"({"path":"../plate.nightfall"})"),
       call(9, "replace_part", R"({"document":"{not json"})"),
       call(10, "export_step", R"({"path":"plate.step","bodies":["plate"]})"),
       call(11, "export_step", R"({"path":"plate.step","bodies":["ghost"]})"),
       call(12, "replace_part", "{\"document\":" + escaped(source(bevelled)) + "}"),
       call(13, "export_step", R"({"path":"bevel.step"})")});
  INFO(process.error);
  REQUIRE(process.status == 0);
  REQUIRE(process.replies.size() == 14);
  // An unsupported body type fails the extrude; the default rolls back.
  const auto& failed = ids(content(process.replies[1], true), "errors");
  REQUIRE_FALSE(failed.empty());
  CHECK(*failed.front().find("code")->if_string() == "unsupported_body_type");
  const auto& kept = content(process.replies[2]);
  CHECK_FALSE(*kept.find("ok")->if_bool());
  CHECK(ids(kept, "removed_bodies") == Array{"plate"});
  CHECK(*ids(content(process.replies[3]), "features")[1].find("status")->if_string() == "failed");
  CHECK(*ids(content(process.replies[4], true), "errors").front().find("code")->if_string() ==
        "empty");
  CHECK(ids(content(process.replies[5]), "added_bodies") == Array{"plate"});
  for (const int i : {6, 7, 8})
    CHECK(*ids(content(process.replies[static_cast<std::size_t>(i)], true), "errors")
               .front()
               .find("code")
               ->if_string() == "path");
  CHECK(*ids(content(process.replies[9], true), "errors").front().find("code")->if_string() ==
        "parse");
  const auto& exported = content(process.replies[10]);
  CHECK(number(exported, "bodies") == 1);
  CHECK(read(temp.path("plate.step")).size() ==
        static_cast<std::size_t>(number(exported, "bytes")));
  CHECK_THAT(reload(temp.path("plate.step"))->measures().volume, WithinAbs(6000, 1e-5));
  CHECK(*ids(content(process.replies[11], true), "errors").front().find("code")->if_string() ==
        "unknown_id");
  CHECK_THAT(number(body(content(process.replies[12]), "plate"), "volume"),
             WithinAbs(6000 - (30 - 4. / 3), 1e-6));
  const auto shape = reload(temp.path("bevel.step"));
  CHECK(shape->measures().faces == 10);
  CHECK_FALSE(fs::exists(temp.path("broken.step")));
}

TEST_CASE("mcp rejects malformed protocol traffic without stopping", "[cli-mcp]") {
  Temp temp;
  const auto process =
      serve(temp, {},
            {"{not json", "[]", R"({"jsonrpc":"1.0","id":1,"method":"ping"})",
             request(2, "nothing/here"), R"({"jsonrpc":"2.0","method":"nothing/here"})",
             request(3, "tools/call", R"({"name":"nope"})"),
             request(4, "tools/call", R"({"name":"measure","arguments":{"body":7}})"),
             "[" + request(5, "ping") + "," + request(6, "ping") + "]", call(7, "inspect_part")});
  INFO(process.error);
  REQUIRE(process.status == 0);
  REQUIRE(process.replies.size() == 8);
  const auto code = [](const Value& reply) {
    REQUIRE(reply.find("error"));
    return static_cast<int>(*reply.find("error")->find("code")->if_number());
  };
  CHECK(code(process.replies[0]) == -32700);
  CHECK(process.replies[0].find("id")->is_null());
  CHECK(code(process.replies[1]) == -32600);
  CHECK(code(process.replies[2]) == -32600);
  CHECK(code(process.replies[3]) == -32601);
  CHECK(code(process.replies[4]) == -32602);
  CHECK(code(process.replies[5]) == -32602);
  REQUIRE(process.replies[6].if_array());
  CHECK(process.replies[6].if_array()->size() == 2);
  // No --in: an empty part with no bodies, and no folder to save into.
  CHECK(ids(content(process.replies[7]), "bodies").empty());
  const auto usage = serve(temp, {"--in"}, {});
  CHECK(usage.status == 2);
  CHECK(usage.replies.empty());
}

TEST_CASE("mcp patch_part applies RFC 6902 operations atomically and rebuilds", "[cli-mcp]") {
  Temp temp;
  const auto input = temp.path("plate.nightfall");
  const auto text = source(part(2));
  write(input, text);
  const auto process = serve(
      temp, {"--in", input.string()},
      {kInitialize,
       // Change the depth parameter in place: volume 10 x 20 x 50.
       call(1, "patch_part",
            R"({"patch":[{"op":"replace","path":"/params/0/value","value":"50 mm"}]})"),
       // Add a parameter and a chamfer feature that uses it.
       call(2, "patch_part", R"({"patch":[
         {"op":"add","path":"/params/-","value":{"name":"bevel","value":"1 mm"}},
         {"op":"add","path":"/features/-","value":{"id":"bevel_top","type":"chamfer","inputs":{
           "distance":"bevel","edges":{"edges_of":{"created_by":"base","role":"cap_end","expect":"one"}}}}}]})"),
       // A failing second operation leaves the first unapplied.
       call(3, "patch_part", R"({"patch":[{"op":"replace","path":"/params/0/value","value":"7 mm"},
                                   {"op":"remove","path":"/features/9"}]})"),
       call(4, "measure", R"({"body":"plate"})"),
       // A patch that makes a feature fail rolls back by default.
       call(
           5, "patch_part",
           R"({"patch":[{"op":"replace","path":"/features/1/inputs/result/body_type","value":"mesh"}]})"),
       call(6, "patch_part", R"({"patch":[{"op":"test","path":"/params/3/name","value":"bevel"},
                                   {"op":"move","from":"/params/3","path":"/params/0"}]})"),
       call(7, "patch_part", R"({"patch":{"op":"remove","path":"/meta"}})"), call(8, "undo"),
       call(9, "inspect_part")});
  INFO(process.error);
  REQUIRE(process.status == 0);
  REQUIRE(process.replies.size() == 10);
  const auto& deeper = content(process.replies[1]);
  CHECK(ids(deeper, "changed_bodies") == Array{"plate"});
  CHECK_THAT(number(body(deeper, "plate"), "volume"), WithinAbs(10000, 1e-6));
  const auto& bevelled = content(process.replies[2]);
  CHECK(*bevelled.find("ok")->if_bool());
  CHECK_THAT(number(body(bevelled, "plate"), "volume"), WithinAbs(10000 - (30 - 4. / 3), 1e-6));
  CHECK(number(body(bevelled, "plate"), "faces") == 10);
  const auto& atomic = ids(content(process.replies[3], true), "errors");
  REQUIRE(atomic.size() == 1);
  CHECK(*atomic.front().find("pointer")->if_string() == "/patch/1");
  CHECK(*atomic.front().find("message")->if_string() ==
        "remove /features/9: array index out of range");
  CHECK_THAT(number(content(process.replies[4]), "volume"), WithinAbs(10000 - (30 - 4. / 3), 1e-6));
  CHECK(*ids(content(process.replies[5], true), "errors").front().find("code")->if_string() ==
        "unsupported_body_type");
  // Parameters reordered, geometry unchanged: no body deltas.
  const auto& moved = content(process.replies[6]);
  CHECK(ids(moved, "changed_bodies").empty());
  // A patch that is not an array violates the tool schema: a protocol error, not a tool result.
  REQUIRE(process.replies[7].find("error"));
  CHECK(*process.replies[7].find("error")->find("code")->if_number() == -32602);
  CHECK(ids(content(process.replies[8]), "changed_bodies").empty());
  const auto& inspected = content(process.replies[9]);
  CHECK(*ids(inspected, "params")[0].find("name")->if_string() == "depth");
  CHECK(*ids(inspected, "params")[3].find("name")->if_string() == "bevel");
  CHECK(ids(inspected, "features").size() == 3);
  CHECK(read(input) == text);
}

TEST_CASE("mcp adopts an invalid startup document so an agent can repair it", "[cli-mcp]") {
  Temp temp;
  const auto input = temp.path("broken.nightfall");
  // depth lost its unit while its bound keeps one: the document itself no longer validates.
  const auto text = ir::json::write(
      ir::apply_patch(ir::json::parse(source(part())).value(),
                      ir::json::parse(R"([{"op":"replace","path":"/params/0/value","value":30},
                                        {"op":"add","path":"/params/0/min","value":"1 mm"}])")
                          .value())
          .value());
  write(input, text);
  const auto process =
      serve(temp, {"--in", input.string()},
            {kInitialize, call(1, "inspect_part"), call(2, "measure"),
             call(3, "patch_part",
                  R"({"patch":[{"op":"replace","path":"/params/0/value","value":"30 mm"}]})"),
             call(4, "measure", R"({"body":"plate"})"), call(5, "undo")});
  INFO(process.error);
  REQUIRE(process.status == 0);
  REQUIRE(process.replies.size() == 6);
  const auto& inspected = content(process.replies[1]);
  CHECK_FALSE(*inspected.find("ok")->if_bool());
  const auto& errors = ids(inspected, "errors");
  REQUIRE_FALSE(errors.empty());
  CHECK(*errors.front().find("pointer")->if_string() == "/params/0/min");
  CHECK(ids(inspected, "bodies").empty());
  CHECK(ids(inspected, "features").empty());  // nothing parsed: the raw text is the source
  CHECK(ids(content(process.replies[2]), "bodies").empty());
  const auto& repaired = content(process.replies[3]);
  CHECK(*repaired.find("ok")->if_bool());
  CHECK(ids(repaired, "added_bodies") == Array{"plate"});
  CHECK_THAT(number(content(process.replies[4]), "volume"), WithinAbs(6000, 1e-6));
  // The invalid state is not undo history: nothing to undo after the repair.
  CHECK(*ids(content(process.replies[5], true), "errors").front().find("code")->if_string() ==
        "history");
  CHECK(read(input) == text);
}

TEST_CASE("mcp export_mesh writes a closed outward OBJ whose volume matches the solid",
          "[cli-mcp]") {
  Temp temp;
  const auto input = temp.path("plate.nightfall");
  write(input, source(part()));
  const auto process =
      serve(temp, {"--in", input.string()},
            {kInitialize, call(1, "export_mesh", R"({"path":"plate.obj"})"),
             call(2, "export_mesh", R"({"path":"ghost.obj","bodies":["ghost"]})"),
             call(3, "export_mesh", R"({"path":"bad.obj","linear_deflection":"fine"})")});
  INFO(process.error);
  REQUIRE(process.status == 0);
  REQUIRE(process.replies.size() == 4);
  const auto& exported = content(process.replies[1]);
  CHECK(*exported.find("path")->if_string() == temp.path("plate.obj").string());
  // Parse the OBJ: positions, then triangles; signed volume by the divergence theorem.
  std::vector<math::Vec3> positions;
  std::size_t triangles = 0;
  double volume = 0;
  std::istringstream obj(read(temp.path("plate.obj")));
  bool named = false;
  for (std::string line; std::getline(obj, line);) {
    std::istringstream fields(line);
    std::string tag;
    fields >> tag;
    if (tag == "o") named = line == "o plate";
    if (tag == "v") {
      math::Vec3 p;
      fields >> p.x >> p.y >> p.z;
      positions.push_back(p);
    } else if (tag == "f") {
      std::array<math::Vec3, 3> corner;
      for (auto& c : corner) {
        std::string ref;
        fields >> ref;
        c = positions.at(static_cast<std::size_t>(std::stoul(ref.substr(0, ref.find('/')))) - 1);
      }
      ++triangles;
      const auto& [a, b, c] = corner;
      volume += (a.x * (b.y * c.z - b.z * c.y) - a.y * (b.x * c.z - b.z * c.x) +
                 a.z * (b.x * c.y - b.y * c.x)) /
                6;
    }
  }
  CHECK(named);
  CHECK(number(exported, "vertices") == static_cast<double>(positions.size()));
  CHECK(number(exported, "triangles") == static_cast<double>(triangles));
  CHECK(triangles >= 12);
  CHECK_THAT(volume, WithinAbs(6000, 1e-6));  // closed and outward: flat faces mesh exactly
  CHECK(*ids(content(process.replies[2], true), "errors").front().find("code")->if_string() ==
        "unknown_id");
  REQUIRE(process.replies[3].find("error"));
  CHECK(*process.replies[3].find("error")->find("code")->if_number() == -32602);
  CHECK_FALSE(fs::exists(temp.path("ghost.obj")));
}

TEST_CASE("mcp body_topology lists faces and edges with generative names and frames", "[cli-mcp]") {
  Temp temp;
  const auto input = temp.path("plate.nightfall");
  write(input, source(part()));
  const auto washer = fs::path(NF_REFERENCE_PARTS_DIR) / "washer.nightfall";
  const auto process =
      serve(temp, {"--in", input.string(), "--allow-path", washer.parent_path().string()},
            {kInitialize, call(1, "body_topology", R"({"body":"plate"})"),
             call(2, "body_topology", R"({"body":"ghost"})"),
             call(3, "open_part", "{\"path\":" + ir::json::canonical(Value(washer.string())) + "}"),
             call(4, "body_topology", R"({"body":"part"})")});
  INFO(process.error);
  REQUIRE(process.status == 0);
  REQUIRE(process.replies.size() == 5);
  const auto& plate = content(process.replies[1]);
  const auto& faces = ids(plate, "faces");
  REQUIRE(faces.size() == 6);
  std::set<std::string> roles;
  for (const auto& face : faces) {
    CHECK(*face.find("type")->if_string() == "plane");
    CHECK(number(face, "edges") == 4);
    const auto& names = ids(face, "names");
    REQUIRE_FALSE(names.empty());
    for (const auto& n : names) {
      CHECK(*n.find("created_by")->if_string() == "base");
      roles.insert(*n.find("role")->if_string());
    }
  }
  CHECK(roles.count("cap_start") == 1);
  CHECK(roles.count("cap_end") == 1);
  CHECK(roles.count("side") == 1);
  const auto& edges = ids(plate, "edges");
  REQUIRE(edges.size() == 12);
  for (const auto& edge : edges) CHECK(ids(edge, "faces").size() == 2);
  CHECK(*ids(content(process.replies[2], true), "errors").front().find("code")->if_string() ==
        "unknown_id");
  // The washer: two cylindrical radii, axes along Z.
  const auto& ring = content(process.replies[4]);
  std::set<int> radii;
  for (const auto& face : ids(ring, "faces"))
    if (*face.find("type")->if_string() == "cylinder") {
      radii.insert(static_cast<int>(std::lround(number(face, "radius"))));
      const auto& axis = *face.find("axis")->if_array();
      CHECK_THAT(std::abs(*axis[2].if_number()), WithinAbs(1, 1e-12));
    }
  CHECK(radii == std::set<int>{5, 12});
}

namespace {
/// A hand-drawn loop with full constraints, in the given length unit. Coordinates are rough.
std::string rough_loop(const char* unit, double scale, const char* x0 = "0.3",
                       bool redundant = false) {
  auto n = [&](double v) { return ir::json::format_number(v * scale); };
  std::string text = R"({"format":"nightfall.part","version":1,"units":{"length":")" +
                     std::string(unit) + R"(","angle":"deg"},"meta":{},
    "params":[{"name":"width","value":"10 mm"},{"name":"height","value":"20 mm"}],"references":[],
    "features":[{"id":"outline","type":"sketch","inputs":{"plane":{"datum":"xy"},"entities":[
      {"id":"origin","type":"point","at":[0,0]},
      {"id":"a","type":"line","start":[X0,)" +
                     n(-0.2) + "],\"end\":[" + n(9.1) + "," + n(0.4) +
                     R"(]},
      {"id":"b","type":"line","start":[)" +
                     n(9.1) + "," + n(0.4) + "],\"end\":[" + n(8.7) + "," + n(19.3) + R"(]},
      {"id":"c","type":"line","start":[)" +
                     n(8.7) + "," + n(19.3) + "],\"end\":[" + n(-0.4) + "," + n(18.8) + R"(]},
      {"id":"d","type":"line","start":[)" +
                     n(-0.4) + "," + n(18.8) + "],\"end\":[" + n(0.3) + "," + n(-0.2) +
                     R"(]}],"constraints":[
      {"type":"fixed","entities":["origin"]},
      {"type":"coincident","entities":[{"entity":"a","point":"start"},"origin"]},
      {"type":"coincident","entities":[{"entity":"a","point":"end"},{"entity":"b","point":"start"}]},
      {"type":"coincident","entities":[{"entity":"b","point":"end"},{"entity":"c","point":"start"}]},
      {"type":"coincident","entities":[{"entity":"c","point":"end"},{"entity":"d","point":"start"}]},
      {"type":"coincident","entities":[{"entity":"d","point":"end"},{"entity":"a","point":"start"}]},
      {"type":"horizontal","entities":["a"]},{"type":"vertical","entities":["b"]},
      {"type":"parallel","entities":["a","c"]},{"type":"perpendicular","entities":["a","d"]},
      {"type":"distance","entities":[{"entity":"a","point":"start"},{"entity":"a","point":"end"}],"value":"width"},
      {"type":"distance","entities":[{"entity":"b","point":"start"},{"entity":"b","point":"end"}],"value":"height"}]}},
    {"id":"base","type":"extrude","inputs":{"profile":{"sketch":"outline"},"distance":"30 mm",
      "result":{"new":"plate","body_type":"brep"}}}],"history_end":null,"ui":{}})";
  text.replace(text.find("X0"), 2, std::string(x0) == "0.3" ? n(0.3) : std::string(x0));
  // d is already vertical through its perpendicular to the horizontal a.
  if (redundant) text.replace(text.find("]}},"), 0, R"(,{"type":"vertical","entities":["d"]})");
  return text;
}
const Value* feature(const Value& build, const char* id) {
  for (const auto& f : *build.find("features")->if_array())
    if (*f.find("id")->if_string() == id) return &f;
  return nullptr;
}
}  // namespace

TEST_CASE("mcp reports sketch freedom and writes solved coordinates back", "[cli-mcp][sketch]") {
  Temp temp;
  for (const auto& [unit, scale] : {std::pair{"mm", 1.0}, {"in", 1 / 25.4}}) {
    CAPTURE(unit);
    const auto input = temp.path("rough.nightfall");
    write(input, rough_loop(unit, scale, "0.3", true));
    const auto process = serve(temp, {"--in", input.string()},
                               {kInitialize, call(1, "inspect_part"),
                                call(2, "save_part", R"({"path":"saved.nightfall"})"),
                                call(3, "open_part", R"({"path":"saved.nightfall"})")});
    INFO(process.error);
    REQUIRE(process.status == 0);
    REQUIRE(process.replies.size() == 4);
    // Startup solved the rough sketch and adopted the solved numbers.
    const auto& inspected = content(process.replies[1]);
    CHECK(*inspected.find("ok")->if_bool());
    const auto* outline = feature(inspected, "outline");
    REQUIRE(outline);
    REQUIRE(outline->find("sketch"));
    const auto& report = *outline->find("sketch");
    CHECK(number(report, "dof") == 0);
    CHECK(*report.find("fully_constrained")->if_bool());
    CHECK(ids(report, "underconstrained").empty());
    // The extra vertical on d adds nothing: it is named, and the sketch still solves.
    CHECK_FALSE(ids(report, "redundant").empty());
    const auto& plate = body(inspected, "plate");
    CHECK_THAT(number(plate, "volume"), WithinAbs(6000, 6000 * 1e-9));
    // The saved document holds the solved coordinates in its own unit.
    const auto saved = ir::json::parse(read(temp.path("saved.nightfall"))).value();
    const auto& entities =
        *saved.find("features")->if_array()->front().find("inputs")->find("entities");
    const auto& c = (*entities.if_array())[3];
    CHECK_THAT(*c.find("start")->if_array()->at(0).if_number(), WithinAbs(10 * scale, 1e-9));
    CHECK_THAT(*c.find("start")->if_array()->at(1).if_number(), WithinAbs(20 * scale, 1e-9));
    // Reopened, nothing needs solving and nothing is written back.
    const auto& reopened = content(process.replies[3]);
    CHECK(*feature(reopened, "outline")->find("sketch")->find("solved")->if_bool() == false);
    CHECK_FALSE(reopened.find("written_back"));
    CHECK_THAT(number(body(reopened, "plate"), "volume"), WithinAbs(6000, 6000 * 1e-9));
  }
}

TEST_CASE("mcp write-back keeps expressions and reports free sketches", "[cli-mcp][sketch]") {
  Temp temp;
  const auto input = temp.path("rough.nightfall");
  write(input, rough_loop("mm", 1, "\"width - 9.7 mm\""));
  auto loose = ir::json::parse(R"({"format":"nightfall.part","version":1,
    "units":{"length":"mm","angle":"deg"},"meta":{},"params":[],"references":[],
    "features":[{"id":"s","type":"sketch","inputs":{"plane":{"datum":"xy"},"entities":[
      {"id":"l","type":"line","start":[0,0],"end":[5,1]},{"id":"p","type":"point","at":[2,3]}],
      "constraints":[{"type":"horizontal","entities":["l"]}]}}],"history_end":null,"ui":{}})")
                   .value();
  const auto process =
      serve(temp, {"--in", input.string()},
            {kInitialize,
             call(1, "patch_part",
                  R"({"patch":[{"op":"replace","path":"/params/0/value","value":"12 mm"}]})"),
             call(2, "get_part"),
             call(3, "replace_part",
                  "{\"document\":" + ir::json::canonical(Value(ir::json::write(loose))) + "}"),
             call(4, "inspect_part")});
  INFO(process.error);
  REQUIRE(process.status == 0);
  REQUIRE(process.replies.size() == 5);
  // a's start is the expression "width - 9.7 mm": it is solved every rebuild and never overwritten.
  const auto& patched = content(process.replies[1]);
  CHECK_THAT(number(body(patched, "plate"), "volume"), WithinAbs(12 * 20 * 30, 7200 * 1e-9));
  const auto& written = ids(patched, "written_back");
  REQUIRE(written.size() == 1);
  CHECK(*written[0].find("feature")->if_string() == "outline");
  CHECK(ids(written[0], "expressions") == Array{"a"});
  const auto text = *content(process.replies[2]).find("document")->if_string();
  CHECK(text.find("width - 9.7 mm") != std::string::npos);
  // A horizontal line and a free point: 3 + 2 degrees of freedom, both named.
  const auto& free = content(process.replies[4]);
  const auto& report = *feature(free, "s")->find("sketch");
  CHECK(number(report, "dof") == 5);
  CHECK_FALSE(*report.find("fully_constrained")->if_bool());
  CHECK(ids(report, "underconstrained") == Array{"l", "p"});
  CHECK_FALSE(*report.find("solved")->if_bool());
  // Rectangles are outside the solver: no report rather than a wrong one.
  const auto washer_like = serve(
      temp, {"--in", (fs::path(NF_REFERENCE_PARTS_DIR) / "mounting_plate.nightfall").string()},
      {kInitialize, call(1, "inspect_part")});
  REQUIRE(washer_like.replies.size() == 2);
  CHECK_FALSE(feature(content(washer_like.replies[1]), "outline")->find("sketch"));
}

namespace {
/// RFC 0013 four-bar of one parametric link (ground 100, crank 40, coupler 120, rocker 80 mm);
/// the crank angle is the parameter `theta`, and revolute joints sit on the pin holes' axes.
constexpr const char* kFourBar = R"({"format":"nightfall.part","version":2,
  "units":{"length":"mm","angle":"deg"},"meta":{},
  "params":[{"name":"length","value":"100 mm"},{"name":"theta","value":"60 deg"}],
  "references":[],"features":[],"history_end":null,"ui":{},
  "components":[{"id":"link","features":[
    {"id":"outline","type":"sketch","inputs":{"plane":{"datum":"xy"},"entities":[
      {"id":"r","type":"rect","corner":[-8,-8],"size":["length + 16 mm",16]},
      {"id":"h0","type":"circle","center":[0,0],"radius":3},
      {"id":"h1","type":"circle","center":["length",0],"radius":3}],"constraints":[]}},
    {"id":"bar","type":"extrude","inputs":{"profile":{"sketch":"outline"},"distance":"5 mm",
      "result":{"new":"bar","body_type":"brep"}}}]}],
  "occurrences":[
    {"id":"ground","component":"link","grounded":true},
    {"id":"crank","component":"link","params":{"length":"40 mm"},
     "placement":{"translation":[0,0,0],"rotation":[0,0,65]}},
    {"id":"coupler","component":"link","params":{"length":"120 mm"},
     "placement":{"translation":[21,34,0],"rotation":[0,0,-10]}},
    {"id":"rocker","component":"link","params":{"length":"80 mm"},
     "placement":{"translation":[100,0,0],"rotation":[0,0,80]}}],
  "joints":[
    {"id":"input","kind":"revolute","value":"theta",
     "a":{"occurrence":"ground","frame":{"created_by":"bar","role":"side","from":"outline/h0"}},
     "b":{"occurrence":"crank","frame":{"created_by":"bar","role":"side","from":"outline/h0"}}},
    {"id":"elbow","kind":"revolute",
     "a":{"occurrence":"crank","frame":{"created_by":"bar","role":"side","from":"outline/h1"}},
     "b":{"occurrence":"coupler","frame":{"created_by":"bar","role":"side","from":"outline/h0"}}},
    {"id":"wrist","kind":"revolute",
     "a":{"occurrence":"coupler","frame":{"created_by":"bar","role":"side","from":"outline/h1"}},
     "b":{"occurrence":"rocker","frame":{"created_by":"bar","role":"side","from":"outline/h1"}}},
    {"id":"pivot","kind":"revolute",
     "a":{"occurrence":"ground","frame":{"created_by":"bar","role":"side","from":"outline/h1"}},
     "b":{"occurrence":"rocker","frame":{"created_by":"bar","role":"side","from":"outline/h0"}}}]})";
const Value& occurrence(const Value& assembly, const char* id) {
  for (const auto& o : *assembly.find("occurrences")->if_array())
    if (*o.find("id")->if_string() == id) return o;
  FAIL("no occurrence " << id);
  return assembly;
}
/// The crank's far pin centre (local (40, 0, 2.5)) in world coordinates.
std::array<double, 3> crank_pin(const Value& assembly) {
  const auto& o = occurrence(assembly, "crank");
  const auto& r = *o.find("rotation")->if_array();
  const auto& t = *o.find("translation")->if_array();
  std::array<double, 3> p{};
  for (std::size_t i = 0; i < 3; ++i)
    p[i] = *r[3 * i].if_number() * 40 + *r[3 * i + 2].if_number() * 2.5 + *t[i].if_number();
  return p;
}
void solved(const Value& assembly) {
  INFO(ir::json::canonical(assembly));
  CHECK(*assembly.find("solved")->if_bool());
  CHECK(number(assembly, "dof") == 0);
  CHECK(number(assembly, "loops") == 1);
  REQUIRE(ids(assembly, "joints").size() == 4);
  for (const auto& j : ids(assembly, "joints")) {
    CHECK(number(j, "residual_mm") <= 1e-9);
    CHECK(number(j, "residual_rad") <= 1e-9);
  }
}
}  // namespace

TEST_CASE("mcp solves, drives and exports a four-bar assembly", "[cli-mcp][assembly]") {
  Temp temp;
  const auto input = temp.path("four_bar.nightfall");
  write(input, kFourBar);
  const auto process =
      serve(temp, {"--in", input.string()},
            {kInitialize, call(1, "inspect_assembly"), call(2, "inspect_part"),
             call(3, "drive_joints", R"({"values":{"input":"90 deg"}})"),
             // Driving the rocker as well over-constrains the loop: rolled back.
             call(4, "drive_joints", R"({"values":{"pivot":"150 deg"}})"),
             call(5, "drive_joints", R"({"values":{"missing":"1 deg"}})"),
             call(6, "measure", R"({"body":"crank/bar"})"),
             call(7, "export_step", R"({"path":"crank.step","bodies":["crank/bar"]})"),
             call(8, "get_part"), call(9, "undo"), call(10, "inspect_assembly")});
  INFO(process.error);
  REQUIRE(process.status == 0);
  REQUIRE(process.replies.size() == 11);
  const auto& initial = content(process.replies[1]);
  solved(initial);
  auto pin = crank_pin(initial);
  CHECK_THAT(std::abs(std::atan2(pin[1], pin[0])), WithinAbs(std::numbers::pi / 3, 1e-9));
  CHECK_THAT(pin[2], WithinAbs(2.5, 1e-9));
  CHECK(ids(occurrence(initial, "crank"), "bodies") == Array{"crank/bar"});
  // The placed bodies are the part's results, each with its component's exact volume.
  const auto bar = [](double length) {
    return ((length + 16) * 16 - 2 * std::numbers::pi * 9) * 5;
  };
  const auto& part = content(process.replies[2]);
  CHECK(*part.find("ok")->if_bool());
  CHECK(ids(part, "bodies").size() == 4);
  for (const auto& [id, length] : {std::pair{"ground/bar", 100.0},
                                   {"crank/bar", 40.0},
                                   {"coupler/bar", 120.0},
                                   {"rocker/bar", 80.0}}) {
    CAPTURE(id);
    CHECK_THAT(number(body(part, id), "volume"), WithinAbs(bar(length), 1e-6));
  }
  CHECK(part.find("assembly"));
  // Driven to 90 degrees: the loop re-solves, and only the moving links change.
  const auto& driven = content(process.replies[3]);
  solved(*driven.find("assembly"));
  pin = crank_pin(*driven.find("assembly"));
  CHECK_THAT(std::abs(std::atan2(pin[1], pin[0])), WithinAbs(std::numbers::pi / 2, 1e-9));
  const auto changed = ids(driven, "changed_bodies");
  CHECK(std::ranges::find(changed, Value("ground/bar")) == changed.end());
  CHECK(std::ranges::find(changed, Value("crank/bar")) != changed.end());
  const auto& conflict = ids(content(process.replies[4], true), "errors");
  REQUIRE_FALSE(conflict.empty());
  CHECK(conflict.front().find("pointer")->if_string()->starts_with("/joints/"));
  CHECK(conflict.front().find("message")->if_string()->find("joints conflict") !=
        std::string::npos);
  CHECK(*ids(content(process.replies[5], true), "errors").front().find("code")->if_string() ==
        "unknown_id");
  // The conflict rolled back: the crank is still at 90 degrees, measured and exported.
  CHECK_THAT(number(content(process.replies[6]), "volume"), WithinAbs(bar(40), 1e-6));
  content(process.replies[7]);
  const auto exported = reload(temp.path("crank.step"));
  CHECK_THAT(exported->measures().volume, WithinAbs(bar(40), 1e-6));
  const auto document = ir::json::parse(*content(process.replies[8]).find("document")->if_string());
  REQUIRE(document);
  const auto& joints = *document->find("joints")->if_array();
  CHECK(*joints[0].find("value")->if_string() == "90 deg");
  CHECK_FALSE(joints[3].find("value"));
  // Undo restores the parameter-driven drive.
  content(process.replies[9]);
  pin = crank_pin(content(process.replies[10]));
  CHECK_THAT(std::abs(std::atan2(pin[1], pin[0])), WithinAbs(std::numbers::pi / 3, 1e-9));
  CHECK(read(input) == kFourBar);
}

TEST_CASE("mcp inspect_assembly reports a plain part as not an assembly", "[cli-mcp][assembly]") {
  Temp temp;
  const auto input = temp.path("plate.nightfall");
  write(input, source(part(2)));
  const auto process = serve(temp, {"--in", input.string()},
                             {kInitialize, call(1, "inspect_assembly"),
                              call(2, "drive_joints", R"({"values":{"a":"1 deg"}})")});
  REQUIRE(process.replies.size() == 3);
  CHECK(*ids(content(process.replies[1], true), "errors").front().find("code")->if_string() ==
        "not_an_assembly");
  CHECK(*ids(content(process.replies[2], true), "errors").front().find("code")->if_string() ==
        "unknown_id");
  CHECK_FALSE(content(process.replies[2], true).find("assembly"));
}

namespace {
/// Two 10 mm blocks, the upper one on a vertical slider over the lower one's top face; the
/// slider value is the gap (negative: the blocks overlap by that depth over 10 x 10 mm).
std::string stacked_blocks(const char* gap) {
  return std::string(R"({"format":"nightfall.part","version":2,
  "units":{"length":"mm","angle":"deg"},"meta":{},"params":[],"references":[],"features":[],
  "history_end":null,"ui":{},
  "components":[{"id":"block","features":[
    {"id":"square","type":"sketch","inputs":{"plane":{"datum":"xy"},"entities":[
      {"id":"r","type":"rect","corner":[-5,-5],"size":[10,10]}],"constraints":[]}},
    {"id":"cube","type":"extrude","inputs":{"profile":{"sketch":"square"},"distance":"10 mm",
      "result":{"new":"cube","body_type":"brep"}}}]}],
  "occurrences":[{"id":"base","component":"block","grounded":true},
    {"id":"top","component":"block","placement":{"translation":[0,0,11]}}],
  "joints":[{"id":"lift","kind":"slider","value":")") +
         gap + R"(",
    "a":{"occurrence":"base","frame":{"created_by":"cube","role":"cap_end"}},
    "b":{"occurrence":"top","frame":{"created_by":"cube","role":"cap_start"},"flip":true}}]})";
}
}  // namespace

TEST_CASE("mcp reports interference, clearance and a motion sweep", "[cli-mcp][assembly]") {
  Temp temp;
  const auto input = temp.path("blocks.nightfall");
  write(input, stacked_blocks("-2 mm"));
  const auto process =
      serve(temp, {"--in", input.string()},
            {kInitialize, call(1, "check_interference"),
             call(2, "measure_clearance", R"({"a":"base","b":"top/cube"})"),
             call(3, "sweep_joint", R"({"joint":"lift","from":-4,"to":4,"steps":8})"),
             call(4, "drive_joints", R"({"values":{"lift":"3 mm"}})"),
             call(5, "measure_clearance", R"({"a":"top","b":"base"})"),
             call(6, "check_interference", R"({"occurrences":["top"]})"),
             call(7, "check_interference", R"({"occurrences":["nowhere"]})"),
             call(8, "sweep_joint", R"({"joint":"missing","from":0,"to":1,"steps":1})"),
             call(9, "sweep_joint", R"({"joint":"lift","from":0,"to":1,"steps":0})"),
             call(10, "get_part")});
  INFO(process.error);
  REQUIRE(process.status == 0);
  REQUIRE(process.replies.size() == 11);
  // 2 mm deep over 10 x 10 mm.
  const auto& clash = content(process.replies[1]);
  REQUIRE(ids(clash, "overlapping").size() == 1);
  const auto pair = ids(clash, "overlapping").front();
  CHECK(*pair.find("a")->if_string() == "base/cube");
  CHECK(*pair.find("b")->if_string() == "top/cube");
  CHECK_THAT(number(pair, "volume"), WithinAbs(200, 200 * 1e-6));
  CHECK(number(clash, "pairs_checked") == 1);
  CHECK(number(content(process.replies[2]), "distance") == 0);
  // The sweep re-solves 9 positions without changing the document.
  const auto& sweep = content(process.replies[3]);
  CHECK(number(sweep, "solved_steps") == 9);
  CHECK(number(sweep, "steps_with_overlap") == 4);
  const auto steps = ids(sweep, "steps");
  REQUIRE(steps.size() == 9);
  for (const auto& step : steps) {
    const double gap = number(step, "value");
    CAPTURE(gap);
    CHECK(*step.find("solved")->if_bool());
    const auto& placement = *step.find("placements")->find("top")->if_array();
    CHECK_THAT(*placement[11].if_number(), WithinAbs(10 + gap, 1e-9));
    const auto& interference = *step.find("interference");
    const auto overlapping = ids(interference, "overlapping");
    if (gap < 0) {
      REQUIRE(overlapping.size() == 1);
      CHECK_THAT(number(overlapping.front(), "volume"), WithinAbs(-100 * gap, 1e-4));
    } else {
      CHECK(overlapping.empty());
    }
    CHECK(ids(interference, "touching").size() == (gap == 0 ? 1u : 0u));
  }
  // Lifted 3 mm: the gap is the clearance, between the facing caps.
  const auto& clear = content(process.replies[5]);
  CHECK_THAT(number(clear, "distance"), WithinAbs(3, 1e-9));
  CHECK(*clear.find("a")->if_string() == "top/cube");
  CHECK_THAT(*clear.find("on_a")->if_array()->at(2).if_number(), WithinAbs(13, 1e-9));
  CHECK_THAT(*clear.find("on_b")->if_array()->at(2).if_number(), WithinAbs(10, 1e-9));
  CHECK(ids(content(process.replies[6]), "overlapping").empty());
  CHECK(*ids(content(process.replies[7], true), "errors").front().find("code")->if_string() ==
        "unknown_id");
  CHECK(*ids(content(process.replies[8], true), "errors").front().find("code")->if_string() ==
        "unknown_id");
  REQUIRE(process.replies[9].find("error"));  // steps outside 1..360: invalid parameters
  // Only drive_joints changed the document; the sweep left the 3 mm value in place.
  const auto text = *content(process.replies[10]).find("document")->if_string();
  CHECK(text.find("\"3 mm\"") != std::string::npos);
}

TEST_CASE("mcp reports mass properties from component and occurrence densities",
          "[cli-mcp][assembly]") {
  Temp temp;
  auto text = stacked_blocks("5 mm");
  // Steel cubes, except the top one, which overrides its component's density with aluminium.
  text.replace(text.find(R"({"id":"block",)"), 14, R"({"id":"block","density":7850,)");
  text.replace(text.find(R"({"id":"top",)"), 12, R"({"id":"top","density":2700,)");
  const auto input = temp.path("blocks.nightfall");
  write(input, text);
  const auto plate = temp.path("plate.nightfall");
  write(plate, source(part(2)));
  const auto process =
      serve(temp, {"--in", input.string()},
            {kInitialize, call(1, "mass_properties"),
             call(2, "mass_properties", R"({"occurrence":"top"})"),
             call(3, "mass_properties", R"({"occurrence":"nowhere"})"),
             call(4, "open_part", R"({"path":"plate.nightfall"})"), call(5, "mass_properties")});
  INFO(process.error);
  REQUIRE(process.status == 0);
  REQUIRE(process.replies.size() == 6);
  const auto& all = content(process.replies[1]);
  // Base cube z 0..10, top cube z 15..25: kg = kg/m^3 x mm^3 x 1e-9.
  const double m_base = 7850 * 1000e-9, m_top = 2700 * 1000e-9, m = m_base + m_top;
  const double z = (m_base * 5 + m_top * 20) / m;
  const auto occurrences = ids(all, "occurrences");
  REQUIRE(occurrences.size() == 2);
  CHECK(number(occurrences[0], "density") == 7850);
  CHECK(number(occurrences[1], "density") == 2700);
  CHECK_THAT(number(occurrences[1], "mass"), WithinAbs(m_top, 1e-9 * m_top));
  const auto& total = *all.find("total");
  CHECK_THAT(number(total, "mass"), WithinAbs(m, 1e-9 * m));
  CHECK_THAT(*total.find("center")->if_array()->at(2).if_number(), WithinAbs(z, 1e-9));
  // kg m^2: each cube m s^2 / 6 about its own axes, plus m dz^2 about x and y.
  const auto& inertia = *total.find("inertia")->if_array();
  const auto entry = [&](std::size_t i, std::size_t j) {
    return *inertia[i].if_array()->at(j).if_number();
  };
  const double izz = m * 100.0 / 6 * 1e-6;
  const double ixx = izz + (m_base * (z - 5) * (z - 5) + m_top * (20 - z) * (20 - z)) * 1e-6;
  CHECK_THAT(entry(2, 2), WithinAbs(izz, 1e-9 * izz));
  CHECK_THAT(entry(0, 0), WithinAbs(ixx, 1e-9 * ixx));
  CHECK_THAT(entry(1, 1), WithinAbs(ixx, 1e-9 * ixx));
  CHECK_THAT(entry(0, 1), WithinAbs(0, 1e-12 * ixx));
  CHECK(ids(all, "without_density").empty());
  const auto& top = content(process.replies[2]);
  REQUIRE(ids(top, "occurrences").size() == 1);
  CHECK_THAT(number(*top.find("total"), "mass"), WithinAbs(m_top, 1e-9 * m_top));
  CHECK(*ids(content(process.replies[3], true), "errors").front().find("code")->if_string() ==
        "unknown_id");
  // A part without densities reports volume only.
  const auto& part_mass = content(process.replies[5]);
  CHECK(ids(part_mass, "without_density") == Array{"plate"});
  CHECK_FALSE(part_mass.find("total"));
  CHECK(ids(part_mass, "occurrences").front().find("density")->is_null());
}

#ifdef NF_WITH_STUDIES
TEST_CASE("mcp runs engineering studies out of process", "[cli-mcp][study]") {
  Temp temp;
  const auto input = temp.path("cantilever.nightfall");
  write(input, read(fs::path(NF_STUDY_EXAMPLES_DIR) / "cantilever.nightfall"));
  const auto process = serve(
      temp, {"--in", input.string()},
      {kInitialize, call(1, "list_studies"), call(2, "run_study", R"({"id":"tip_load"})"),
       call(3, "study_results", R"({"id":"tip_load"})"),
       call(4, "set_parameters", R"({"values":{"length":"100 mm"}})"),
       call(5, "study_results", R"({"id":"tip_load"})"),
       call(6, "run_study", R"({"id":"tip_load"})"), call(7, "run_study", R"({"id":"missing"})"),
       call(8, "run_study", R"({"id":"tip_load","budget_s":0.01})")});
  INFO(process.error);
  REQUIRE(process.status == 0);
  REQUIRE(process.replies.size() == 9);
  const auto studies = ids(content(process.replies[1]), "studies");
  REQUIRE(studies.size() == 1);
  CHECK(*studies[0].find("kind")->if_string() == "static_structural");
  // 10 N at the tip of a 200 mm aluminium beam: PL^3 / 3EI = 0.4638 mm.
  const double beam = 10 * std::pow(200.0, 3) / (3 * 69000 * (1e4 / 12));
  const auto& first = content(process.replies[2]);
  const double long_tip = number(*first.find("quantities"), "max_displacement");
  CHECK_THAT(long_tip, WithinRel(beam, 0.02));
  CHECK(*first.find("passed")->if_bool());
  CHECK(ir::json::canonical(content(process.replies[3])) == ir::json::canonical(first));
  // After an edit the cached result no longer describes the document.
  CHECK(*ids(content(process.replies[5], true), "errors").front().find("code")->if_string() ==
        "not_run");
  // Half the length: a cantilever's tip deflection scales with L^3 (shear adds under 1%).
  const double short_tip =
      number(*content(process.replies[6]).find("quantities"), "max_displacement");
  CHECK_THAT(short_tip / long_tip, WithinRel(0.125, 0.01));
  const auto missing = ids(content(process.replies[7], true), "errors");
  CHECK(*missing.front().find("code")->if_string() == "unknown_id");
  CHECK(*missing.front().find("pointer")->if_string() == "/studies");
  CHECK(*ids(content(process.replies[8], true), "errors").front().find("code")->if_string() ==
        "budget");
}
#endif
