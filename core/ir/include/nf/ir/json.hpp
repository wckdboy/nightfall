// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// Strict JSON for the feature IR (RFC 0005): RFC 8259 with the I-JSON restrictions (RFC 7493:
// valid UTF-8, no lone surrogates, no duplicate keys, finite numbers). Input is untrusted, so
// every malformed construct is an error and nesting depth is bounded.

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace nf::ir::json {

struct Member;
class Value;
using Array = std::vector<Value>;
using Object = std::vector<Member>;  // in document order

/// A JSON value: null, boolean, number (a double), string (UTF-8), array or object.
class Value {
 public:
  // Defined after Member, which must be complete first.
  Value();  // null
  Value(std::nullptr_t);
  Value(bool b);
  Value(double x);
  Value(std::string s);
  Value(const char* s);
  Value(Array a);
  Value(Object o);

  [[nodiscard]] bool is_null() const { return std::holds_alternative<std::nullptr_t>(data_); }
  [[nodiscard]] const bool* if_bool() const { return std::get_if<bool>(&data_); }
  [[nodiscard]] const double* if_number() const { return std::get_if<double>(&data_); }
  [[nodiscard]] const std::string* if_string() const { return std::get_if<std::string>(&data_); }
  [[nodiscard]] const Array* if_array() const { return std::get_if<Array>(&data_); }
  [[nodiscard]] const Object* if_object() const { return std::get_if<Object>(&data_); }

  /// The number, if it is an integer of magnitude ≤ 2^53 (exactly representable).
  [[nodiscard]] std::optional<std::int64_t> if_integer() const;
  /// The member's value, if this is an object with that key.
  [[nodiscard]] const Value* find(std::string_view key) const;

  /// Content equality (offsets are ignored; NaN, which JSON cannot hold, is unequal).
  friend bool operator==(const Value& a, const Value& b);

  std::size_t offset = 0;  // byte offset in the parsed text; 0 for built values

 private:
  std::variant<std::nullptr_t, bool, double, std::string, Array, Object> data_;
};

struct Member {
  std::string key;
  Value value;
  friend bool operator==(const Member&, const Member&) = default;
};

inline Value::Value() = default;
inline Value::Value(std::nullptr_t) {}
inline Value::Value(bool b) : data_(b) {}
inline Value::Value(double x) : data_(x) {}
inline Value::Value(std::string s) : data_(std::move(s)) {}
inline Value::Value(const char* s) : data_(std::string(s)) {}
inline Value::Value(Array a) : data_(std::move(a)) {}
inline Value::Value(Object o) : data_(std::move(o)) {}

struct ParseError {
  std::size_t offset = 0;  // byte offset of the problem
  std::string message;
};

inline constexpr int kMaxDepth = 256;

/// Parses one JSON text (no byte order mark, no trailing content).
[[nodiscard]] std::expected<Value, ParseError> parse(std::string_view text);

}  // namespace nf::ir::json
