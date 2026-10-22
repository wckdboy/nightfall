// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "kernel_serve.hpp"

#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string_view>
#include <thread>

#include "nf/ir/json.hpp"
#include "nf/kernel/kernel.hpp"

namespace nf::cli {
namespace {
using ir::json::Array;
using ir::json::Object;
using ir::json::Value;
using Clock = std::chrono::steady_clock;

constexpr int kProtocol = 1;
constexpr std::size_t kMaxLine = std::size_t{64} << 20;
constexpr double kDefaultSampleTolerance = 1e-3;

/// A request that fails with an RFC 0015 error code; the server keeps serving.
struct Failure {
  std::string code, message;
};
[[noreturn]] void fail(std::string code, std::string message) {
  throw Failure{std::move(code), std::move(message)};
}
template <class T>
T value_or_fail(std::expected<T, kernel::KernelError> result, const char* code) {
  if (!result) fail(code, result.error().message);
  return std::move(*result);
}

const Value& member(const Value& request, const char* key) {
  const auto* v = request.find(key);
  if (!v) fail("bad_request", std::string(key) + " is required");
  return *v;
}
double number(const Value& request, const char* key) {
  const auto* v = member(request, key).if_number();
  if (!v) fail("bad_request", std::string(key) + " must be a number");
  return *v;
}
const Array& array(const Value& v, const char* what) {
  const auto* a = v.if_array();
  if (!a) fail("bad_request", std::string(what) + " must be an array");
  return *a;
}
math::Vec3 point(const Value& v) {
  const auto& a = array(v, "a sample point");
  if (a.size() != 3 || !a[0].if_number() || !a[1].if_number() || !a[2].if_number())
    fail("bad_request", "a sample point is three numbers");
  return {*a[0].if_number(), *a[1].if_number(), *a[2].if_number()};
}
std::size_t index(const Value& v, std::size_t size, const char* what) {
  const auto i = v.if_integer();
  if (!i || *i < 0 || static_cast<std::uint64_t>(*i) >= size)
    fail("bad_request", std::string(what) + " must index the operand's edges");
  return static_cast<std::size_t>(*i);
}

/// One solid with the client's face and edge numbering mapped onto the kernel's.
struct Operand {
  kernel::ShapePtr shape;
  std::vector<kernel::EntityPtr> faces;               // kernel faces
  std::vector<std::vector<std::size_t>> face_owners;  // client faces of each kernel face
  std::vector<math::Vec3> edge_points;                // client edge samples
  std::vector<kernel::EntityPtr> edges;               // kernel edges, unique
};

class Server {
 public:
  explicit Server(kernel::IKernel& kernel) : kernel_(kernel) {}

  Object handle(const Value& request) {
    const auto* op = member(request, "op").if_string();
    if (!op) fail("bad_request", "op must be a string");
    if (*op == "hello")
      return {{"protocol", static_cast<double>(kProtocol)},
              {"kernel", std::string(kernel_.identity())},
              {"ops", Array{"boolean", "fillet", "chamfer"}}};
    tolerance_ = kDefaultSampleTolerance;
    if (const auto* t = request.find("sample_tolerance")) {
      if (!t->if_number() || !(*t->if_number() >= 0))
        fail("bad_request", "sample_tolerance must be a nonnegative number");
      tolerance_ = *t->if_number();
    }
    tolerance_ += 10 * math::Tolerance::standard().linear;
    std::vector<Operand> operands;
    for (const auto& v : array(member(request, "operands"), "operands"))
      operands.push_back(operand(v, operands.size()));
    if (operands.empty()) fail("bad_request", "operands must not be empty");
    kernel::OperationResult result;
    if (*op == "boolean") {
      const auto* kind = member(request, "kind").if_string();
      const auto boolean = !kind                  ? std::nullopt
                           : *kind == "union"     ? std::optional{kernel::BooleanOp::kUnion}
                           : *kind == "subtract"  ? std::optional{kernel::BooleanOp::kSubtract}
                           : *kind == "intersect" ? std::optional{kernel::BooleanOp::kIntersect}
                                                  : std::nullopt;
      if (!boolean) fail("bad_request", "kind must be union, subtract or intersect");
      if (operands.size() < 2) fail("bad_request", "a boolean needs a target and tools");
      std::vector<kernel::ShapePtr> tools;
      for (std::size_t i = 1; i < operands.size(); ++i) tools.push_back(operands[i].shape);
      result = value_or_fail(kernel_.boolean_operation(operands[0].shape, tools, *boolean),
                             "operation_failed");
    } else if (*op == "fillet" || *op == "chamfer") {
      if (operands.size() != 1) fail("bad_request", *op + " takes one operand");
      std::vector<kernel::EntityPtr> edges;
      for (const auto& v : array(member(request, "edges"), "edges"))
        edges.push_back(edge(operands[0], index(v, operands[0].edge_points.size(), "edges")));
      result = value_or_fail(
          *op == "fillet" ? kernel_.fillet(operands[0].shape, edges, number(request, "radius"))
                          : kernel_.chamfer(operands[0].shape, edges, number(request, "distance")),
          "operation_failed");
    } else {
      fail("unsupported", "unknown op: " + *op);
    }
    return response(operands, result);
  }

 private:
  Operand operand(const Value& v, std::size_t number) {
    const auto* step = member(v, "step").if_string();
    if (!step) fail("bad_request", "an operand's step must be a string");
    auto solids = value_or_fail(kernel_.import_step(*step), "step_read");
    if (solids.size() != 1) fail("step_read", "an operand is exactly one solid");
    Operand result{solids.front(), {}, {}, {}, {}};
    for (auto& face : value_or_fail(kernel_.faces(result.shape), "step_read"))
      result.faces.push_back(std::move(face.entity));
    result.face_owners.resize(result.faces.size());
    const auto& samples = array(member(v, "faces"), "faces");
    for (std::size_t i = 0; i < samples.size(); ++i) {
      const auto p = point(samples[i]);
      std::vector<std::size_t> near;
      for (std::size_t f = 0; f < result.faces.size(); ++f)
        if (value_or_fail(kernel_.face_distance(result.faces[f], p), "unmatched_face") <=
            tolerance_)
          near.push_back(f);
      if (near.size() != 1)
        fail("unmatched_face", "operand " + std::to_string(number) + " face sample " +
                                   std::to_string(i) + " lies on " + std::to_string(near.size()) +
                                   " faces");
      result.face_owners[near.front()].push_back(i);
    }
    if (const auto* edges = v.find("edges"))
      for (const auto& e : array(*edges, "edges")) result.edge_points.push_back(point(e));
    return result;
  }
  const std::vector<kernel::EntityPtr>& edges(Operand& operand) {
    if (operand.edges.empty())
      for (const auto& face : operand.faces)
        for (auto& e : value_or_fail(kernel_.edges(face), "operation_failed"))
          if (std::ranges::none_of(operand.edges, [&](const auto& x) { return x->same_as(*e); }))
            operand.edges.push_back(std::move(e));
    return operand.edges;
  }
  /// The kernel edge under the client's edge sample `i`; exactly one must lie within tolerance.
  kernel::EntityPtr edge(Operand& operand, std::size_t i) {
    std::vector<kernel::EntityPtr> near;
    for (const auto& e : edges(operand))
      if (value_or_fail(kernel_.edge_distance(e, operand.edge_points[i]), "unmatched_edge") <=
          tolerance_)
        near.push_back(e);
    if (near.size() != 1)
      fail("unmatched_edge", "edge sample " + std::to_string(i) + " lies on " +
                                 std::to_string(near.size()) + " edges");
    return near.front();
  }
  /// The client's edges whose samples lie on a kernel edge.
  std::vector<std::size_t> edge_owners(const Operand& operand, const kernel::EntityPtr& e) {
    std::vector<std::size_t> result;
    for (std::size_t i = 0; i < operand.edge_points.size(); ++i)
      if (value_or_fail(kernel_.edge_distance(e, operand.edge_points[i]), "operation_failed") <=
          tolerance_)
        result.push_back(i);
    return result;
  }
  double area(const kernel::EntityPtr& face) {
    return value_or_fail(kernel_.face_mass(face), "operation_failed").area;
  }

  Object response(std::vector<Operand>& operands, const kernel::OperationResult& result) {
    auto step = value_or_fail(kernel_.export_step({result.shape}), "operation_failed");
    auto owner = [&](const kernel::EntityPtr& e) -> std::optional<std::size_t> {
      for (std::size_t s = 0; s < operands.size(); ++s)
        if (e->owner() == operands[s].shape) return s;
      return std::nullopt;
    };
    Array faces;
    for (const auto& info : value_or_fail(kernel_.faces(result.shape), "operation_failed")) {
      const auto& face = info.entity;
      Array from;
      std::size_t inputs = 0;  // kernel input faces this face was made from
      const kernel::History* only = nullptr;
      for (const auto& h : result.history) {
        const auto s = owner(h.before);
        if (!s) continue;
        auto& operand = operands[*s];
        const auto has = [&](const auto& list) {
          return std::ranges::any_of(list, [&](const auto& x) { return x->same_as(*face); });
        };
        if (h.before->kind() == kernel::EntityKind::kFace && has(h.modified)) {
          ++inputs;
          only = &h;
          for (std::size_t f = 0; f < operand.faces.size(); ++f)
            if (operand.faces[f]->same_as(*h.before))
              for (auto i : operand.face_owners[f])
                from.push_back(
                    Object{{"operand", static_cast<double>(*s)}, {"face", static_cast<double>(i)}});
        } else if (h.before->kind() == kernel::EntityKind::kEdge && has(h.generated)) {
          for (auto i : edge_owners(operand, h.before))
            from.push_back(
                Object{{"operand", static_cast<double>(*s)}, {"edge", static_cast<double>(i)}});
        }
      }
      const auto here = area(face);
      std::string change = "generated";
      if (inputs > 1) {
        change = "merged";
      } else if (inputs == 1 && only->modified.size() > 1) {
        change = "split";
      } else if (inputs == 1) {
        const auto before = area(only->before);
        change = std::abs(here - before) <= 1e-9 * std::max(1.0, before) ? "kept" : "modified";
      }
      Object entry{{"change", change}, {"from", std::move(from)}, {"area", here}};
      for (const auto& g : result.generated)
        if (g.entity->same_as(*face)) entry.push_back({"role", g.role});
      faces.push_back(std::move(entry));
    }
    return {{"kernel", std::string(kernel_.identity())},
            {"step", std::move(step)},
            {"faces", std::move(faces)}};
  }

  kernel::IKernel& kernel_;
  double tolerance_ = kDefaultSampleTolerance;
};

/// Reads one line of at most kMaxLine bytes; nullopt at end of input.
std::optional<std::string> next_line(std::streambuf& in, bool& too_long) {
  std::string line;
  for (int c = in.sbumpc(); c != std::char_traits<char>::eof(); c = in.sbumpc()) {
    if (c == '\n') return line;
    if (line.size() == kMaxLine) {
      too_long = true;
      return line;
    }
    line.push_back(static_cast<char>(c));
  }
  if (line.empty()) return std::nullopt;
  return line;
}
void send(int fd, const Value& response) {
  auto text = ir::json::canonical(response);
  text.push_back('\n');
  for (std::string_view rest = text; !rest.empty();) {
    const auto n = ::write(fd, rest.data(), rest.size());
    if (n <= 0) throw std::runtime_error("cannot write a response");
    rest.remove_prefix(static_cast<std::size_t>(n));
  }
}
}  // namespace

int run_kernel_serve(int argc, char** argv) {
  double timeout = 120;
  for (int i = 3; i < argc; i += 2) {
    const std::string_view option(argv[i]);
    // std::from_chars for double is unavailable on macOS 15.
    const char* value = i + 1 < argc ? argv[i + 1] : "";
    char* end = nullptr;
    timeout = std::strtod(value, &end);
    if (option != "--timeout" || end == value || *end || !(timeout > 0) ||
        !std::isfinite(timeout)) {
      std::cerr << "usage: nf kernel serve [--timeout SECONDS]\n";
      return 2;
    }
  }
  // Libraries print to stdout (OCCT's STEP parser does); keep it for protocol lines only.
  const int out = ::dup(STDOUT_FILENO);
  if (out < 0 || ::dup2(STDERR_FILENO, STDOUT_FILENO) < 0) {
    std::cerr << "fatal: cannot reserve stdout\n";
    return 1;
  }
  try {
    auto kernel = kernel::make_occt_kernel();
    if (!kernel) {
      std::cerr << "kernel unavailable: " << kernel.error().message << '\n';
      return 1;
    }
    Server server{**kernel};
    // RFC 0003's watchdog contract: a hung operation ends the process, naming the request.
    std::atomic<Clock::rep> started{0};
    std::atomic<bool> done{false};
    std::mutex naming;
    std::string current;  // the running request's id, for the watchdog's report
    std::thread watchdog([&] {
      const auto limit =
          std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(timeout));
      while (!done) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const auto t = started.load();
        if (t && Clock::now() - Clock::time_point(Clock::duration(t)) > limit) {
          const std::lock_guard lock(naming);
          std::fprintf(stderr, "nf kernel serve: request %s exceeded %gs\n", current.c_str(),
                       timeout);
          std::_Exit(3);
        }
      }
    });
    auto& in = *std::cin.rdbuf();
    int status = 0;
    bool too_long = false;
    while (auto line = next_line(in, too_long)) {
      if (too_long) {
        send(out, Object{{"id", nullptr},
                         {"ok", false},
                         {"error", Object{{"code", "too_large"},
                                          {"message", "request lines are capped at 64 MiB"}}}});
        status = 1;
        break;
      }
      if (line->find_first_not_of(" \t\r") == std::string::npos) continue;
      const auto parsed = ir::json::parse(*line);
      const Value* id = parsed && parsed->if_object() ? parsed->find("id") : nullptr;
      Object response{{"id", id ? *id : Value(nullptr)}};
      const auto t0 = Clock::now();
      try {
        if (!parsed) fail("bad_request", parsed.error().message);
        if (!parsed->if_object()) fail("bad_request", "a request is a JSON object");
        {
          const std::lock_guard lock(naming);
          current = id ? ir::json::canonical(*id) : "null";
        }
        started = t0.time_since_epoch().count();
        auto body = server.handle(*parsed);
        started = 0;
        response.push_back({"ok", true});
        for (auto& m : body) response.push_back(std::move(m));
      } catch (const Failure& f) {
        started = 0;
        response.push_back({"ok", false});
        response.push_back({"error", Object{{"code", f.code}, {"message", f.message}}});
      } catch (const std::exception& e) {
        started = 0;
        response.push_back({"ok", false});
        response.push_back({"error", Object{{"code", "operation_failed"}, {"message", e.what()}}});
      }
      response.push_back(
          {"ms", std::chrono::duration<double, std::milli>(Clock::now() - t0).count()});
      send(out, response);
    }
    done = true;
    watchdog.join();
    return status;
  } catch (const std::exception& error) {
    std::cerr << "fatal: " << error.what() << '\n';
    return 1;
  }
}

}  // namespace nf::cli
