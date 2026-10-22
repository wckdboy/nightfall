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
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

extern char** environ;
using namespace nf;
using namespace nf::kernel_service::test;
using Catch::Matchers::WithinAbs;
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
  CHECK(names == std::vector<std::string>{"inspect_part", "get_schema", "get_part", "measure",
                                          "set_parameters", "replace_part", "patch_part", "undo",
                                          "redo", "open_part", "save_part", "export_step"});
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
