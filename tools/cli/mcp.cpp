// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "mcp.hpp"

#include <unistd.h>
#include <cstdlib>
#include <iostream>
#include <string_view>

#include "nf/ir/patch.hpp"
#include "session.hpp"

namespace nf::cli {
namespace {
namespace fs = std::filesystem;
using ir::json::Array;
using ir::json::Object;
using ir::json::Value;

constexpr std::string_view kVersions[] = {"2025-06-18", "2025-03-26", "2024-11-05"};
constexpr std::string_view kInstructions =
    "Nightfall part documents are JSON feature programs (RFC 0005): named parameters with units, "
    "an ordered feature list and selectors instead of indices. Read the schema with get_schema, "
    "the current part with inspect_part (or its raw text with get_part), its faces and edges "
    "with body_topology, change it with "
    "set_parameters, patch_part (RFC 6902) "
    "or replace_part, then "
    "verify measured volume, area, centroid and bounds with measure. Mutating tools rebuild and "
    "report the bodies whose geometry changed; a failed feature rolls the change back unless "
    "rollback is false. Every error carries a JSON Pointer into the document.";

/// A JSON-RPC error that escapes a method handler.
struct RpcError {
  int code;
  std::string message;
};
[[noreturn]] void invalid_params(std::string message) {
  throw RpcError{-32602, std::move(message)};
}

const Object& object_arg(const Value& args, const char* key) {
  const auto* v = args.find(key);
  if (!v || !v->if_object()) invalid_params(std::string(key) + " must be an object");
  return *v->if_object();
}
std::optional<std::string> string_arg(const Value& args, const char* key, bool required) {
  const auto* v = args.find(key);
  if (!v) {
    if (required) invalid_params(std::string(key) + " is required");
    return std::nullopt;
  }
  if (!v->if_string()) invalid_params(std::string(key) + " must be a string");
  return *v->if_string();
}
bool bool_arg(const Value& args, const char* key, bool fallback) {
  const auto* v = args.find(key);
  if (!v) return fallback;
  if (!v->if_bool()) invalid_params(std::string(key) + " must be a boolean");
  return *v->if_bool();
}
double number_arg(const Value& args, const char* key, double fallback) {
  const auto* v = args.find(key);
  if (!v) return fallback;
  if (!v->if_number()) invalid_params(std::string(key) + " must be a number");
  return *v->if_number();
}
std::vector<std::string> strings_arg(const Value& args, const char* key) {
  std::vector<std::string> result;
  const auto* v = args.find(key);
  if (!v) return result;
  if (!v->if_array()) invalid_params(std::string(key) + " must be an array of strings");
  for (const auto& item : *v->if_array()) {
    if (!item.if_string()) invalid_params(std::string(key) + " must be an array of strings");
    result.push_back(*item.if_string());
  }
  return result;
}

Object property(const char* type, const char* description) {
  return {{"type", type}, {"description", description}};
}
Object schema(Object properties, Array required = {}) {
  return {{"type", "object"},
          {"properties", std::move(properties)},
          {"required", std::move(required)},
          {"additionalProperties", false}};
}
Object tool(const char* name, const char* description, Object input, bool read_only,
            bool open_world = false) {
  return {{"name", name},
          {"description", description},
          {"inputSchema", std::move(input)},
          {"annotations", Object{{"readOnlyHint", read_only},
                                 {"destructiveHint", !read_only},
                                 {"idempotentHint", read_only},
                                 {"openWorldHint", open_world}}}};
}
Object rollback_property() {
  return property("boolean",
                  "Keep the previous document when any feature fails (default true). With false "
                  "the failing state is kept so it can be inspected and repaired.");
}
Array tools() {
  return {
      tool("inspect_part",
           "Units, metadata, parameters, features with build status and errors, "
           "live bodies with measurements, and the document digest.",
           schema({}), true),
      tool("get_schema", "The JSON Schema of a part document and the feature types it defines.",
           schema({}), true),
      tool("get_part", "The current document text (JSON) and its SHA-256, valid or not.",
           schema({}), true),
      tool("body_topology",
           "Faces and edges of one live body with their generative names (created_by feature, "
           "role, from sketch entity), analytic type and frame (plane origin/normal/x_axis, "
           "cylinder origin/axis/radius) and adjacency. Indices are display order only: build "
           "selectors from the names, for example {\"edges_of\": {\"created_by\": \"base\", "
           "\"role\": \"cap_end\"}}.",
           schema({{"body", property("string", "A live body id")}}, {"body"}), true),
      tool("measure",
           "Volume (mm^3), area (mm^2), centroid, bounds and topology counts of one live body, "
           "or of every live body with totals.",
           schema({{"body", property("string", "A live body id; omit for all bodies")}}), true),
      tool("set_parameters",
           "Set named parameter values or expressions (for example \"2 * wall + 1 mm\"), "
           "revalidate and rebuild. Unknown names are errors.",
           schema({{"values", Object{{"type", "object"},
                                     {"description", "parameter name to value or expression"},
                                     {"additionalProperties", Object{{"type", "string"}}}}},
                   {"rollback", rollback_property()}},
                  {"values"}),
           false),
      tool("replace_part", "Replace the whole document with new JSON text, validate and rebuild.",
           schema({{"document", property("string", "A complete part document (JSON text)")},
                   {"rollback", rollback_property()}},
                  {"document"}),
           false),
      tool(
          "patch_part",
          "Apply an RFC 6902 JSON Patch (add, remove, replace, move, copy, test; RFC 6901 "
          "pointers such as /params/0/value or /features/-) to the document, validate and "
          "rebuild. Atomic: a failing operation is reported as /patch/<index> and nothing changes.",
          schema({{"patch", Object{{"type", "array"},
                                   {"items", Object{{"type", "object"}}},
                                   {"description", "RFC 6902 operations, applied in order"}}},
                  {"rollback", rollback_property()}},
                 {"patch"}),
          false),
      tool("inspect_assembly",
           "The assembly solve (RFC 0013): status, degrees of freedom, loops, each joint's "
           "residuals in mm and rad, and each occurrence's placement and bodies.",
           schema({}), true),
      tool("drive_joints",
           "Set joint values by joint id (an angle or length, or a list per degree of freedom, "
           "for example {\"input\": \"75 deg\"}) and re-solve the assembly.",
           schema({{"values", Object{{"type", "object"},
                                     {"description", "joint id to value, expression or list"},
                                     {"additionalProperties", true}}},
                   {"rollback", rollback_property()}},
                  {"values"}),
           false),
      tool("check_interference",
           "Bodies of different occurrences that overlap (with their exact common volume in "
           "mm^3) or touch, at the current solved placements (RFC 0013).",
           schema(
               {{"occurrences",
                 Object{{"type", "array"},
                        {"items", Object{{"type", "string"}}},
                        {"description", "keep pairs involving these occurrences; omit for all"}}}}),
           true),
      tool("measure_clearance",
           "Minimum distance in mm between two occurrences or bodies, with the closest bodies "
           "and points (0 when they touch or overlap).",
           schema({{"a", property("string", "occurrence id or occurrence/body id")},
                   {"b", property("string", "occurrence id or occurrence/body id")}},
                  {"a", "b"}),
           true),
      tool("mass_properties",
           "Mass, center of mass and inertia tensor (kg m^2 about the center, assembly axes) "
           "per occurrence and for the assembly, from exact volume integrals and the density "
           "(kg/m^3) on each occurrence or its component.",
           schema({{"occurrence", property("string", "one occurrence; omit for all")}}), true),
      tool(
          "sweep_joint",
          "Motion study: drive a revolute, cylindrical or slider joint from `from` to `to` "
          "(document angle or length units) in `steps` intervals, re-solving each step without "
          "changing the document; reports placements and, unless disabled, interference.",
          schema(
              {{"joint", property("string", "joint id")},
               {"from", property("number", "first value")},
               {"to", property("number", "last value")},
               {"steps", property("integer", "intervals, 1 to 360")},
               {"interference", property("boolean", "check interference each step (default true)")},
               {"occurrences",
                Object{{"type", "array"},
                       {"items", Object{{"type", "string"}}},
                       {"description", "occurrences whose placements to report; omit for all"}}}},
              {"joint", "from", "to", "steps"}),
          true),
      tool("undo", "Restore the document before the last change.", schema({}), false),
      tool("redo", "Reapply the last undone change.", schema({}), false),
      tool("open_part", "Load a .nightfall document from an allowed folder as the session.",
           schema({{"path", property("string", "Path to a .nightfall file")},
                   {"rollback", rollback_property()}},
                  {"path"}),
           false, true),
      tool("save_part", "Write the current document (default: the --in path).",
           schema({{"path", property("string", "Destination; replaced atomically")}}), false, true),
      tool("export_step",
           "Write the live bodies (all, or the named ones) as exact STEP AP242 in millimetres.",
           schema({{"path", property("string", "Destination .step file")},
                   {"bodies", Object{{"type", "array"},
                                     {"items", Object{{"type", "string"}}},
                                     {"description", "live body ids; omit for all"}}}},
                  {"path"}),
           false, true),
      tool("export_mesh",
           "Write a derived Wavefront OBJ mesh of the live bodies (all, or the named ones) with "
           "outward normals, for viewing or downstream tools. Geometry stays exact in the "
           "document; the mesh is a cache at the given deflections.",
           schema(
               {{"path", property("string", "Destination .obj file")},
                {"bodies", Object{{"type", "array"},
                                  {"items", Object{{"type", "string"}}},
                                  {"description", "live body ids; omit for all"}}},
                {"linear_deflection", property("number", "chord error in mm (default 0.1)")},
                {"angular_deflection", property("number", "angle error in radians (default 0.2)")}},
               {"path"}),
           false, true),
  };
}

/// Files reachable by tools: the --in folder, the private scratch folder and --allow-path
/// entries. Hidden entries below a root are refused; paths resolve before comparison.
class Roots {
 public:
  void add(const fs::path& directory) { roots_.push_back(fs::weakly_canonical(directory)); }
  [[nodiscard]] const fs::path& base() const { return roots_.front(); }
  [[nodiscard]] std::expected<fs::path, ir::Errors> resolve(const std::string& text) const {
    if (text.empty())
      return std::unexpected(ir::Errors{{"path", "path must not be empty", "", {}}});
    fs::path given(text);
    const auto path = fs::weakly_canonical(given.is_absolute() ? given : base() / given);
    for (const auto& root : roots_) {
      auto r = root.begin();
      auto p = path.begin();
      for (; r != root.end() && p != path.end() && *r == *p; ++r, ++p) {
      }
      if (r != root.end() || p == path.end()) continue;
      bool hidden = false;
      for (; p != path.end(); ++p) hidden = hidden || p->string().starts_with('.');
      if (hidden) break;
      return path;
    }
    return std::unexpected(
        ir::Errors{{"path", "path is outside the allowed folders: " + path.string(), "", {}}});
  }

 private:
  std::vector<fs::path> roots_;
};

struct Server {
  Session& session;
  Roots roots;
  std::optional<fs::path> document;

  Object tool_result(const Value& body, bool is_error) {
    Object result{{"content", Array{Object{{"type", "text"}, {"text", ir::json::canonical(body)}}}},
                  {"structuredContent", body}};
    if (is_error) result.push_back({"isError", true});
    return result;
  }
  Object outcome(std::expected<Session::Build, ir::Errors> build) {
    if (!build) return tool_result(Object{{"errors", error_entries(build.error())}}, true);
    return tool_result(build->json(), false);
  }
  Object write_file(const std::string& path, std::string_view text, Object extra) {
    const auto resolved = roots.resolve(path);
    if (!resolved) return tool_result(Object{{"errors", error_entries(resolved.error())}}, true);
    publish(*resolved, text);
    extra.insert(extra.begin(), {{"path", resolved->string()},
                                 {"bytes", static_cast<double>(text.size())},
                                 {"sha256", ir::sha256(text).hex()}});
    return tool_result(std::move(extra), false);
  }

  Object call(const std::string& name, const Value& args) {
    if (name == "inspect_part") return tool_result(session.inspect(), false);
    if (name == "get_schema") {
      auto parsed = ir::json::parse(ir::part_schema());
      return tool_result(Object{{"schema", std::move(*parsed)}}, false);
    }
    if (name == "get_part")
      return tool_result(
          Object{{"document", session.source()}, {"sha256", ir::sha256(session.source()).hex()}},
          false);
    if (name == "body_topology")
      return outcome_object(session.topology(*string_arg(args, "body", true)));
    if (name == "measure") return outcome_object(session.measure(string_arg(args, "body", false)));
    if (name == "set_parameters") {
      std::map<std::string, std::string> values;
      for (const auto& m : object_arg(args, "values")) {
        if (m.value.if_string())
          values[m.key] = *m.value.if_string();
        else if (m.value.if_number())
          values[m.key] = ir::json::format_number(*m.value.if_number());
        else
          invalid_params("values." + m.key + " must be a string or number");
      }
      return outcome(session.set_parameters(values, bool_arg(args, "rollback", true)));
    }
    if (name == "replace_part")
      return outcome(
          session.replace(*string_arg(args, "document", true), bool_arg(args, "rollback", true)));
    if (name == "patch_part") {
      const auto* patch = args.find("patch");
      if (!patch || !patch->if_array()) invalid_params("patch must be an array");
      const auto current = ir::json::parse(session.source());
      auto patched = ir::apply_patch(*current, *patch);
      if (!patched) return tool_result(Object{{"errors", error_entries(patched.error())}}, true);
      return outcome(session.replace(ir::json::write(*patched), bool_arg(args, "rollback", true)));
    }
    if (name == "inspect_assembly") {
      if (!session.assembly())
        return tool_result(
            Object{{"errors", Array{Object{{"code", "not_an_assembly"},
                                           {"message", "the document has no occurrences"},
                                           {"pointer", "/occurrences"}}}}},
            true);
      return tool_result(*session.assembly(), false);
    }
    if (name == "drive_joints") {
      auto current = ir::json::parse(session.source()).value();
      const auto* joints = current.find("joints");
      const auto& values = object_arg(args, "values");
      Array patch;
      for (const auto& [joint, value] : values) {
        std::optional<std::size_t> index;
        if (joints && joints->if_array())
          for (std::size_t k = 0; k < joints->if_array()->size(); ++k)
            if (const auto* id = (*joints->if_array())[k].find("id");
                id && id->if_string() && *id->if_string() == joint)
              index = k;
        if (!index)
          return tool_result(Object{{"errors", Array{Object{{"code", "unknown_id"},
                                                            {"message", "no joint " + joint},
                                                            {"pointer", "/joints"}}}}},
                             true);
        patch.push_back(Object{{"op", "add"},
                               {"path", "/joints/" + std::to_string(*index) + "/value"},
                               {"value", value}});
      }
      auto patched = ir::apply_patch(current, patch);
      if (!patched) return tool_result(Object{{"errors", error_entries(patched.error())}}, true);
      return outcome(session.replace(ir::json::write(*patched), bool_arg(args, "rollback", true)));
    }
    if (name == "check_interference")
      return outcome_object(session.interference(strings_arg(args, "occurrences")));
    if (name == "measure_clearance")
      return outcome_object(
          session.clearance(*string_arg(args, "a", true), *string_arg(args, "b", true)));
    if (name == "mass_properties")
      return outcome_object(session.mass_properties(string_arg(args, "occurrence", false)));
    if (name == "sweep_joint") {
      const double steps = number_arg(args, "steps", 0);
      if (steps != std::floor(steps) || steps < 1 || steps > 360)
        invalid_params("steps must be an integer from 1 to 360");
      return outcome_object(
          session.sweep(*string_arg(args, "joint", true), number_arg(args, "from", 0),
                        number_arg(args, "to", 0), static_cast<std::size_t>(steps),
                        bool_arg(args, "interference", true), strings_arg(args, "occurrences")));
    }
    if (name == "undo") return outcome(session.undo());
    if (name == "redo") return outcome(session.redo());
    if (name == "open_part") {
      const auto resolved = roots.resolve(*string_arg(args, "path", true));
      if (!resolved) return tool_result(Object{{"errors", error_entries(resolved.error())}}, true);
      return outcome(session.replace(load(*resolved), bool_arg(args, "rollback", true)));
    }
    if (name == "save_part") {
      auto path = string_arg(args, "path", false);
      if (!path && !document) invalid_params("path is required without --in");
      return write_file(path ? *path : document->string(), session.source(), {});
    }
    if (name == "export_step") {
      const auto bodies = strings_arg(args, "bodies");
      auto encoded = session.export_step(bodies);
      if (!encoded) return tool_result(Object{{"errors", error_entries(encoded.error())}}, true);
      return write_file(*string_arg(args, "path", true), *encoded,
                        {{"bodies", static_cast<double>(bodies.size())}});
    }
    if (name == "export_mesh") {
      kernel::MeshingOptions options;
      options.linear_deflection = number_arg(args, "linear_deflection", options.linear_deflection);
      options.angular_deflection =
          number_arg(args, "angular_deflection", options.angular_deflection);
      const auto bodies = strings_arg(args, "bodies");
      auto mesh = session.export_obj(bodies, options);
      if (!mesh) return tool_result(Object{{"errors", error_entries(mesh.error())}}, true);
      return write_file(*string_arg(args, "path", true), mesh->obj,
                        {{"bodies", static_cast<double>(bodies.size())},
                         {"vertices", static_cast<double>(mesh->vertices)},
                         {"triangles", static_cast<double>(mesh->triangles)}});
    }
    invalid_params("unknown tool: " + name);
  }
  Object outcome_object(std::expected<Object, ir::Errors> result) {
    if (!result) return tool_result(Object{{"errors", error_entries(result.error())}}, true);
    return tool_result(std::move(*result), false);
  }

  std::optional<Value> handle(const std::string& method, const Value& params) {
    if (method == "initialize") {
      std::string version(kVersions[0]);
      if (const auto* requested = params.find("protocolVersion");
          requested && requested->if_string())
        for (const auto v : kVersions)
          if (*requested->if_string() == v) version = std::string(v);
      return Object{{"protocolVersion", version},
                    {"capabilities", Object{{"tools", Object{{"listChanged", false}}}}},
                    {"serverInfo", Object{{"name", "nightfall"}, {"version", "0.0.1"}}},
                    {"instructions", std::string(kInstructions)}};
    }
    if (method == "ping") return Object{};
    if (method == "tools/list") return Object{{"tools", tools()}};
    if (method == "tools/call") {
      const auto name = string_arg(params, "name", true);
      const auto* args = params.find("arguments");
      const Value none = Object{};
      if (args && !args->if_object()) invalid_params("arguments must be an object");
      try {
        return call(*name, args ? *args : none);
      } catch (const ir::Errors& errors) {
        return tool_result(Object{{"errors", error_entries(errors)}}, true);
      } catch (const fs::filesystem_error& error) {
        return tool_result(Object{{"errors", error_entries({{"io", error.what(), "", {}}})}}, true);
      }
    }
    if (method.starts_with("notifications/")) return std::nullopt;
    throw RpcError{-32601, "method not found: " + method};
  }

  /// One request or notification; a response only for requests (those with an id).
  std::optional<Value> dispatch(const Value& message) {
    const auto* object = message.if_object();
    const auto* id = object ? message.find("id") : nullptr;
    Value id_value = id ? *id : Value(nullptr);
    auto reply = [&](Object body) {
      body.insert(body.begin(), {"jsonrpc", "2.0"});
      body.push_back({"id", id_value});
      return Value(std::move(body));
    };
    auto error = [&](int code, std::string text) {
      return reply({{"error", Object{{"code", static_cast<double>(code)}, {"message", text}}}});
    };
    const auto* version = object ? message.find("jsonrpc") : nullptr;
    const auto* method = object ? message.find("method") : nullptr;
    if (!object || !version || !version->if_string() || *version->if_string() != "2.0" || !method ||
        !method->if_string())
      return error(-32600, "invalid request");
    const auto* params = message.find("params");
    const Value none = Object{};
    if (params && !params->if_object()) return error(-32602, "params must be an object");
    try {
      auto result = handle(*method->if_string(), params ? *params : none);
      if (!id) return std::nullopt;
      if (!result) return error(-32601, "method not found: " + *method->if_string());
      return reply({{"result", std::move(*result)}});
    } catch (const RpcError& e) {
      if (!id) return std::nullopt;
      return error(e.code, e.message);
    }
  }
};

struct Options {
  std::optional<std::string> input;
  std::vector<std::string> allow;
};
std::expected<Options, std::string> parse(int argc, char** argv) {
  Options options;
  for (int i = 2; i < argc; i += 2) {
    const std::string_view option(argv[i]);
    if (i + 1 == argc) return std::unexpected("missing option value");
    const std::string value(argv[i + 1]);
    if (value.empty()) return std::unexpected("option value must not be empty");
    if (option == "--in") {
      if (options.input) return std::unexpected("--in must be specified once");
      options.input = value;
    } else if (option == "--allow-path") {
      options.allow.push_back(value);
    } else {
      return std::unexpected("unknown option: " + std::string(option));
    }
  }
  return options;
}
}  // namespace

int run_mcp(int argc, char** argv) {
  auto options = parse(argc, argv);
  if (!options) {
    std::cerr << "usage: nf mcp [--in part.nightfall] [--allow-path DIR ...]: " << options.error()
              << '\n';
    return 2;
  }
  try {
    auto kernel = kernel::make_occt_kernel();
    if (!kernel) {
      std::cerr << "kernel unavailable: " << kernel.error().message << '\n';
      return 1;
    }
    Session session(**kernel, math::Tolerance::standard());
    Server server{session, {}, {}};
    auto scratch = (fs::temp_directory_path() / "nightfall-mcp-XXXXXX").string();
    if (!mkdtemp(scratch.data())) {
      std::cerr << "cannot create the scratch folder\n";
      return 1;
    }
    if (options->input) {
      server.document = fs::weakly_canonical(fs::absolute(*options->input));
      server.roots.add(server.document->parent_path());
      auto text = load(*server.document);
      auto loaded = session.replace(text, false);
      if (loaded) {
        session.clear_history();
      } else {
        // A broken part is still a session: inspect_part reports the errors to repair.
        std::cerr << "nf mcp: document does not validate; see inspect_part errors\n";
        session.adopt_invalid(std::move(text), loaded.error());
      }
    }
    server.roots.add(scratch);
    for (const auto& dir : options->allow) server.roots.add(fs::absolute(dir));
    std::cerr << "nf mcp: scratch " << scratch << (options->input ? ", document " : "")
              << (options->input ? server.document->string() : "") << '\n';
    std::string line;
    while (std::getline(std::cin, line)) {
      if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
      const auto parsed = ir::json::parse(line);
      std::optional<Value> response;
      if (!parsed) {
        response = Object{{"jsonrpc", "2.0"},
                          {"error", Object{{"code", -32700.}, {"message", parsed.error().message}}},
                          {"id", nullptr}};
      } else if (const auto* batch = parsed->if_array()) {
        Array responses;
        for (const auto& item : *batch)
          if (auto one = server.dispatch(item)) responses.push_back(std::move(*one));
        if (batch->empty())
          response = server.dispatch(*parsed);
        else if (!responses.empty())
          response = std::move(responses);
      } else {
        response = server.dispatch(*parsed);
      }
      if (response) std::cout << ir::json::canonical(*response) << '\n' << std::flush;
    }
    std::error_code ignored;
    fs::remove_all(scratch, ignored);
    return 0;
  } catch (const ir::Errors& errors) {
    std::cerr << ir::json::canonical(Object{{"errors", error_entries(errors)}}) << '\n';
    return 1;
  } catch (const std::exception& error) {
    std::cerr << "fatal: " << error.what() << '\n';
    return 1;
  }
}

}  // namespace nf::cli
