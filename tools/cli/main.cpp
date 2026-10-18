// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "evaluator.hpp"

#include <unistd.h>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
namespace fs = std::filesystem;
using namespace nf;
constexpr std::string_view kUsage = "nf build part.nightfall --out part.step";
[[noreturn]] void fail(std::string code, std::string message) {
  throw ir::Errors{{std::move(code), std::move(message), "", {}}};
}
[[noreturn]] void io_error() { fail("io", std::strerror(errno)); }
int report(const ir::Errors& errors, int status = 1) {
  ir::json::Array entries;
  for (const auto& e : errors) {
    ir::json::Object entry{{"code", e.code}, {"message", e.message}, {"pointer", e.pointer}};
    if (e.offset) entry.push_back({"offset", static_cast<double>(*e.offset)});
    entries.push_back(std::move(entry));
  }
  std::cerr << ir::json::canonical(ir::json::Object{{"errors", std::move(entries)}}) << '\n';
  return status;
}
std::string load(const fs::path& path) {
  if (!fs::is_regular_file(path)) fail("io", "input must be a regular file");
  std::ifstream stream(path, std::ios::binary);
  if (!stream) fail("io", "cannot open input");
  std::array<char, 8192> buffer;
  std::string text;
  while (stream) {
    stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = static_cast<std::size_t>(stream.gcount());
    if (count > ir::kMaxDocumentBytes - text.size())
      fail("parse", "document exceeds the 16 MiB limit");
    text.append(buffer.data(), count);
  }
  if (!stream.eof()) fail("io", "cannot read input");
  return text;
}
void publish(const fs::path& output, std::string_view text) {
  auto temporary = (output.parent_path() / ".nightfall-XXXXXX").string();
  int file = mkstemp(temporary.data());
  if (file < 0) io_error();
  try {
    while (!text.empty()) {
      const auto count = ::write(file, text.data(), text.size());
      if (count < 0 && errno == EINTR) continue;
      if (count < 0) io_error();
      if (count == 0) fail("io", "output write made no progress");
      text.remove_prefix(static_cast<std::size_t>(count));
    }
    if (fsync(file) != 0) io_error();
    const int closed = close(file);
    file = -1;
    if (closed != 0) io_error();
    if (std::rename(temporary.c_str(), output.c_str()) != 0) io_error();
  } catch (...) {
    if (file >= 0) close(file);
    std::remove(temporary.c_str());
    throw;
  }
}
}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--help") {
    std::cout << kUsage << '\n';
    return 0;
  }
  if (argc != 5 || std::string_view(argv[1]) != "build" || std::string_view(argv[3]) != "--out")
    return report({{"usage", std::string(kUsage), "", {}}}, 2);
  try {
    const fs::path input(argv[2]), output = fs::absolute(argv[4]);
    if (fs::weakly_canonical(input) == fs::weakly_canonical(output) ||
        (fs::exists(output) && fs::equivalent(input, output)))
      fail("io", "output must not alias the input document");
    const auto source = load(input);
    auto kernel = kernel::make_occt_kernel();
    if (!kernel) fail(kernel.error().code, kernel.error().message);
    kernel_service::Evaluator evaluator(**kernel, {96, 64 * 1024 * 1024});
    auto evaluated = evaluator.evaluate(source);
    if (!evaluated) return report(evaluated.error());
    if (!evaluated->ok()) {
      ir::Errors errors;
      for (const auto& f : evaluated->features)
        errors.insert(errors.end(), f.errors.begin(), f.errors.end());
      return report(errors);
    }
    std::vector<kernel::ShapePtr> shapes;
    ir::json::Array bodies;
    for (const auto& [id, body] : evaluated->bodies)
      if (body.shape) {
        shapes.push_back(body.shape);
        bodies.push_back(id);
      }
    auto encoded = (*kernel)->export_step(shapes);
    if (!encoded) fail(encoded.error().code, encoded.error().message);
    publish(output, *encoded);
    std::cout << ir::json::canonical(ir::json::Object{{"output", std::string(argv[4])},
                                                      {"bodies", std::move(bodies)}})
              << '\n';
    return 0;
  } catch (const ir::Errors& errors) {
    return report(errors);
  } catch (const fs::filesystem_error& error) {
    return report({{"io", error.what(), "", {}}});
  } catch (const std::exception& error) {
    return report({{"internal", error.what(), "", {}}});
  }
}
