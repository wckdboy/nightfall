// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "nf/ir/patch.hpp"

#include <algorithm>

namespace nf::ir {
namespace {
using json::Array;
using json::Object;
using json::Value;
using Tokens = std::vector<std::string>;

/// RFC 6901: "" is the whole document; otherwise "/" separated tokens with ~1 → / and ~0 → ~.
std::expected<Tokens, std::string> parse_pointer(std::string_view text) {
  Tokens tokens;
  if (text.empty()) return tokens;
  if (text.front() != '/') return std::unexpected("pointer must start with '/'");
  std::string token;
  for (std::size_t i = 1; i <= text.size(); ++i) {
    if (i == text.size() || text[i] == '/') {
      tokens.push_back(std::move(token));
      token.clear();
      continue;
    }
    if (text[i] != '~') {
      token.push_back(text[i]);
      continue;
    }
    if (i + 1 < text.size() && text[i + 1] == '0')
      token.push_back('~');
    else if (i + 1 < text.size() && text[i + 1] == '1')
      token.push_back('/');
    else
      return std::unexpected("invalid escape in pointer");
    ++i;
  }
  return tokens;
}

/// An array index token: digits without leading zeros, or "-" (one past the end, add only).
std::optional<std::size_t> parse_index(const std::string& token, std::size_t size, bool allow_end) {
  if (token == "-") return allow_end ? std::optional(size) : std::nullopt;
  if (token.empty() || token.size() > 9) return std::nullopt;
  if (token.size() > 1 && token.front() == '0') return std::nullopt;
  std::size_t index = 0;
  for (const char c : token) {
    if (c < '0' || c > '9') return std::nullopt;
    index = index * 10 + static_cast<std::size_t>(c - '0');
  }
  return index;
}

const Value* get(const Value& root, const Tokens& tokens) {
  const Value* node = &root;
  for (const auto& token : tokens) {
    if (node->if_object()) {
      node = node->find(token);
    } else if (const auto* array = node->if_array()) {
      const auto index = parse_index(token, array->size(), false);
      if (!index || *index >= array->size()) return nullptr;
      node = &(*array)[*index];
    } else {
      return nullptr;
    }
    if (!node) return nullptr;
  }
  return node;
}

enum class Edit { kAdd, kRemove, kReplace };

/// Rebuilds `node` with the edit applied at tokens[i..]; the input is untouched.
std::expected<Value, std::string> edited(const Value& node, const Tokens& tokens, std::size_t i,
                                         Edit edit, const Value* value) {
  if (i == tokens.size()) {
    if (edit == Edit::kRemove) return std::unexpected("cannot remove the document root");
    return *value;
  }
  const auto& token = tokens[i];
  const bool last = i + 1 == tokens.size();
  if (const auto* object = node.if_object()) {
    Object copy = *object;
    const auto found = std::find_if(copy.begin(), copy.end(),
                                    [&](const json::Member& m) { return m.key == token; });
    if (!last) {
      if (found == copy.end()) return std::unexpected("path does not exist");
      auto child = edited(found->value, tokens, i + 1, edit, value);
      if (!child) return std::unexpected(child.error());
      found->value = std::move(*child);
      return Value(std::move(copy));
    }
    if (edit == Edit::kAdd) {
      if (found != copy.end())
        found->value = *value;
      else
        copy.push_back({token, *value});
    } else if (found == copy.end()) {
      return std::unexpected("path does not exist");
    } else if (edit == Edit::kRemove) {
      copy.erase(found);
    } else {
      found->value = *value;
    }
    return Value(std::move(copy));
  }
  if (const auto* array = node.if_array()) {
    Array copy = *array;
    const auto index = parse_index(token, copy.size(), last && edit == Edit::kAdd);
    if (!index) return std::unexpected("invalid array index '" + token + "'");
    if (!last || edit != Edit::kAdd) {
      if (*index >= copy.size()) return std::unexpected("array index out of range");
    } else if (*index > copy.size()) {
      return std::unexpected("array index out of range");
    }
    if (!last) {
      auto child = edited(copy[*index], tokens, i + 1, edit, value);
      if (!child) return std::unexpected(child.error());
      copy[*index] = std::move(*child);
    } else if (edit == Edit::kAdd) {
      copy.insert(copy.begin() + static_cast<std::ptrdiff_t>(*index), *value);
    } else if (edit == Edit::kRemove) {
      copy.erase(copy.begin() + static_cast<std::ptrdiff_t>(*index));
    } else {
      copy[*index] = *value;
    }
    return Value(std::move(copy));
  }
  return std::unexpected("path crosses a scalar");
}

bool is_prefix(const Tokens& prefix, const Tokens& path) {
  return prefix.size() <= path.size() && std::equal(prefix.begin(), prefix.end(), path.begin());
}
}  // namespace

std::expected<Value, Errors> apply_patch(const Value& document, const Value& patch) {
  const auto* operations = patch.if_array();
  if (!operations)
    return std::unexpected(Errors{{"patch", "patch must be an array", "/patch", {}}});
  if (operations->size() > kMaxPatchOperations)
    return std::unexpected(Errors{{"patch", "too many operations", "/patch", {}}});
  Value current = document;
  for (std::size_t i = 0; i < operations->size(); ++i) {
    const auto& operation = (*operations)[i];
    const std::string pointer = "/patch/" + std::to_string(i);
    auto fail = [&](std::string message) {
      return std::unexpected(Errors{{"patch", std::move(message), pointer, {}}});
    };
    if (!operation.if_object()) return fail("operation must be an object");
    const auto* op = operation.find("op");
    const auto* path = operation.find("path");
    if (!op || !op->if_string()) return fail("op must be a string");
    if (!path || !path->if_string()) return fail("path must be a string");
    const std::string& name = *op->if_string();
    auto tokens = parse_pointer(*path->if_string());
    if (!tokens) return fail(name + " " + *path->if_string() + ": " + tokens.error());
    auto describe = [&](const std::string& message) {
      return fail(name + " " + *path->if_string() + ": " + message);
    };
    const auto* value = operation.find("value");
    const auto* from = operation.find("from");
    if (name == "add" || name == "replace") {
      if (!value) return describe("value is required");
      auto result = edited(current, *tokens, 0, name == "add" ? Edit::kAdd : Edit::kReplace, value);
      if (!result) return describe(result.error());
      current = std::move(*result);
    } else if (name == "remove") {
      auto result = edited(current, *tokens, 0, Edit::kRemove, nullptr);
      if (!result) return describe(result.error());
      current = std::move(*result);
    } else if (name == "move" || name == "copy") {
      if (!from || !from->if_string()) return describe("from must be a string");
      auto source = parse_pointer(*from->if_string());
      if (!source) return describe("from: " + source.error());
      const auto* found = get(current, *source);
      if (!found) return describe("from " + *from->if_string() + " does not exist");
      Value moved = *found;
      if (name == "move") {
        if (*source == *tokens) continue;
        if (is_prefix(*source, *tokens)) return describe("cannot move a value into itself");
        auto removed = edited(current, *source, 0, Edit::kRemove, nullptr);
        if (!removed) return describe(removed.error());
        current = std::move(*removed);
      }
      auto result = edited(current, *tokens, 0, Edit::kAdd, &moved);
      if (!result) return describe(result.error());
      current = std::move(*result);
    } else if (name == "test") {
      if (!value) return describe("value is required");
      const auto* found = get(current, *tokens);
      if (!found) return describe("path does not exist");
      if (!(*found == *value)) return describe("value differs");
    } else {
      return fail("unknown op '" + name + "'");
    }
  }
  return current;
}

}  // namespace nf::ir
