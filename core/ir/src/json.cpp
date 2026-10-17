// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "nf/ir/json.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <numeric>

#include <xlocale.h>

namespace nf::ir::json {

std::optional<std::int64_t> Value::if_integer() const {
  const double* x = if_number();
  constexpr double kLimit = 9007199254740992.0;  // 2^53
  if (!x || *x != std::trunc(*x) || std::abs(*x) > kLimit) return std::nullopt;
  return static_cast<std::int64_t>(*x);
}

const Value* Value::find(std::string_view key) const {
  if (const Object* o = if_object()) {
    for (const Member& m : *o) {
      if (m.key == key) return &m.value;
    }
  }
  return nullptr;
}

bool operator==(const Value& a, const Value& b) { return a.data_ == b.data_; }

namespace {

class Parser {
 public:
  explicit Parser(std::string_view s) : s_(s) {}

  std::expected<Value, ParseError> document() {
    if (s_.starts_with("\xef\xbb\xbf")) return fail("a byte order mark is not allowed");
    auto v = value(0);
    if (!v) return v;
    space();
    if (i_ != s_.size()) return fail("unexpected content after the value");
    return v;
  }

 private:
  std::unexpected<ParseError> fail(std::string message) const { return fail_at(i_, message); }
  static std::unexpected<ParseError> fail_at(std::size_t at, std::string message) {
    return std::unexpected(ParseError{at, std::move(message)});
  }

  void space() {
    while (i_ < s_.size() &&
           (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) {
      ++i_;
    }
  }
  bool eat(char c) {
    space();
    if (i_ < s_.size() && s_[i_] == c) {
      ++i_;
      return true;
    }
    return false;
  }

  std::expected<Value, ParseError> value(int depth) {
    space();
    const std::size_t at = i_;
    auto v = bare(depth);
    if (v) v->offset = at;
    return v;
  }

  std::expected<Value, ParseError> bare(int depth) {
    if (i_ >= s_.size()) return fail("expected a value");
    const char c = s_[i_];
    if (c == '{' || c == '[') {
      if (depth >= kMaxDepth) return fail("nesting is too deep");
      return c == '{' ? object(depth + 1) : array(depth + 1);
    }
    if (c == '"') {
      auto str = string();
      if (!str) return std::unexpected(str.error());
      return Value(std::move(*str));
    }
    if (c == '-' || (c >= '0' && c <= '9')) return number();
    for (const auto& [word, v] : {std::pair{std::string_view("null"), Value()},
                                  std::pair{std::string_view("true"), Value(true)},
                                  std::pair{std::string_view("false"), Value(false)}}) {
      if (s_.substr(i_).starts_with(word)) {
        i_ += word.size();
        return v;
      }
    }
    return fail("expected a value");
  }

  std::expected<Value, ParseError> array(int depth) {
    ++i_;
    Array a;
    if (eat(']')) return Value(std::move(a));
    do {
      auto v = value(depth);
      if (!v) return v;
      a.push_back(std::move(*v));
    } while (eat(','));
    if (!eat(']')) return fail("expected ',' or ']'");
    return Value(std::move(a));
  }

  std::expected<Value, ParseError> object(int depth) {
    ++i_;
    Object o;
    if (eat('}')) return Value(std::move(o));
    std::vector<std::size_t> key_offsets;
    do {
      space();
      if (i_ >= s_.size() || s_[i_] != '"') return fail("expected a string key");
      key_offsets.push_back(i_);
      auto key = string();
      if (!key) return std::unexpected(key.error());
      if (!eat(':')) return fail("expected ':'");
      auto v = value(depth);
      if (!v) return v;
      o.push_back(Member{std::move(*key), std::move(*v)});
    } while (eat(','));
    if (!eat('}')) return fail("expected ',' or '}'");
    // I-JSON: keys are unique. Report the later of the first duplicate pair.
    std::vector<std::size_t> order(o.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::ranges::stable_sort(order,
                             [&](std::size_t a, std::size_t b) { return o[a].key < o[b].key; });
    for (std::size_t k = 1; k < order.size(); ++k) {
      if (o[order[k]].key == o[order[k - 1]].key) {
        return fail_at(key_offsets[order[k]], "duplicate key \"" + o[order[k]].key + "\"");
      }
    }
    return Value(std::move(o));
  }

  std::expected<Value, ParseError> number() {
    const std::size_t from = i_;
    const auto digits = [&] {
      const std::size_t d = i_;
      while (i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9') ++i_;
      return i_ - d;
    };
    if (s_[i_] == '-') ++i_;
    const std::size_t int_from = i_;
    const std::size_t n = digits();
    if (n == 0) return fail_at(from, "malformed number");
    if (n > 1 && s_[int_from] == '0') return fail_at(from, "leading zeros are not allowed");
    if (i_ < s_.size() && s_[i_] == '.') {
      ++i_;
      if (digits() == 0) return fail_at(from, "malformed number");
    }
    if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
      ++i_;
      if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) ++i_;
      if (digits() == 0) return fail_at(from, "malformed number");
    }
    // std::from_chars(double) needs macOS 26; strtod_l in the C locale reads the token.
    static const locale_t c_locale = newlocale(LC_ALL_MASK, "C", nullptr);
    const std::string token(s_.substr(from, i_ - from));
    const double x = strtod_l(token.c_str(), nullptr, c_locale);
    if (!std::isfinite(x)) return fail_at(from, "number out of range");
    return Value(x);
  }

  // One UTF-8 sequence starting at i_, validated (no overlongs, surrogates or > U+10FFFF).
  bool utf8(std::string& out) {
    const auto byte = [&](std::size_t k) { return static_cast<unsigned char>(s_[i_ + k]); };
    const unsigned char b0 = byte(0);
    int len = 0;
    unsigned lo = 0x80, hi = 0xbf;  // allowed range of the second byte
    if (b0 >= 0xc2 && b0 <= 0xdf) {
      len = 2;
    } else if (b0 >= 0xe0 && b0 <= 0xef) {
      len = 3;
      if (b0 == 0xe0) lo = 0xa0;
      if (b0 == 0xed) hi = 0x9f;  // excludes the surrogates
    } else if (b0 >= 0xf0 && b0 <= 0xf4) {
      len = 4;
      if (b0 == 0xf0) lo = 0x90;
      if (b0 == 0xf4) hi = 0x8f;
    } else {
      return false;
    }
    if (i_ + static_cast<std::size_t>(len) > s_.size()) return false;
    if (byte(1) < lo || byte(1) > hi) return false;
    for (std::size_t k = 2; k < static_cast<std::size_t>(len); ++k) {
      if (byte(k) < 0x80 || byte(k) > 0xbf) return false;
    }
    out.append(s_.substr(i_, static_cast<std::size_t>(len)));
    i_ += static_cast<std::size_t>(len);
    return true;
  }

  std::optional<unsigned> hex4() {
    if (i_ + 4 > s_.size()) return std::nullopt;
    unsigned v = 0;
    const auto r = std::from_chars(s_.data() + i_, s_.data() + i_ + 4, v, 16);
    if (r.ptr != s_.data() + i_ + 4) return std::nullopt;
    i_ += 4;
    return v;
  }

  static void put(std::string& out, unsigned cp) {
    const auto b = [&](unsigned x) { out.push_back(static_cast<char>(x)); };
    if (cp < 0x80) {
      b(cp);
    } else if (cp < 0x800) {
      b(0xc0 | (cp >> 6));
      b(0x80 | (cp & 0x3f));
    } else if (cp < 0x10000) {
      b(0xe0 | (cp >> 12));
      b(0x80 | ((cp >> 6) & 0x3f));
      b(0x80 | (cp & 0x3f));
    } else {
      b(0xf0 | (cp >> 18));
      b(0x80 | ((cp >> 12) & 0x3f));
      b(0x80 | ((cp >> 6) & 0x3f));
      b(0x80 | (cp & 0x3f));
    }
  }

  std::expected<std::string, ParseError> string() {
    ++i_;  // the opening quote
    std::string out;
    while (true) {
      if (i_ >= s_.size()) return fail("unterminated string");
      const auto c = static_cast<unsigned char>(s_[i_]);
      if (c == '"') {
        ++i_;
        return out;
      }
      if (c < 0x20) return fail("control character in a string");
      if (c >= 0x80) {
        if (!utf8(out)) return fail("invalid UTF-8");
        continue;
      }
      if (c != '\\') {
        out.push_back(static_cast<char>(c));
        ++i_;
        continue;
      }
      const std::size_t at = i_++;
      if (i_ >= s_.size()) return fail("unterminated string");
      const char e = s_[i_++];
      switch (e) {
        case '"':
        case '\\':
        case '/':
          out.push_back(e);
          break;
        case 'b':
          out.push_back('\b');
          break;
        case 'f':
          out.push_back('\f');
          break;
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'u': {
          auto u = hex4();
          if (!u) return fail_at(at, "malformed \\u escape");
          unsigned cp = *u;
          if (cp >= 0xdc00 && cp <= 0xdfff) return fail_at(at, "lone surrogate");
          if (cp >= 0xd800 && cp <= 0xdbff) {
            if (!s_.substr(i_).starts_with("\\u")) return fail_at(at, "lone surrogate");
            i_ += 2;
            auto lo = hex4();
            if (!lo || *lo < 0xdc00 || *lo > 0xdfff) return fail_at(at, "lone surrogate");
            cp = 0x10000 + ((cp - 0xd800) << 10) + (*lo - 0xdc00);
          }
          put(out, cp);
          break;
        }
        default:
          return fail_at(at, "unknown escape");
      }
    }
  }

  std::string_view s_;
  std::size_t i_ = 0;
};

void quote(std::string& out, std::string_view s) {
  out.push_back('"');
  for (const char ch : s) {
    const auto c = static_cast<unsigned char>(ch);
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (c < 0x20) {
          constexpr std::string_view kHex = "0123456789abcdef";
          out += "\\u00";
          out.push_back(kHex[static_cast<std::size_t>(c >> 4)]);
          out.push_back(kHex[static_cast<std::size_t>(c & 0xf)]);
        } else {
          out.push_back(ch);
        }
    }
  }
  out.push_back('"');
}

// UTF-16 code units of a valid UTF-8 string, for JCS key order.
std::u16string utf16(std::string_view s) {
  std::u16string out;
  for (std::size_t i = 0; i < s.size();) {
    const auto b = [&](std::size_t k) {
      return static_cast<unsigned>(static_cast<unsigned char>(s[i + k]));
    };
    unsigned cp = 0;
    if (b(0) < 0x80) {
      cp = b(0);
      i += 1;
    } else if (b(0) < 0xe0) {
      cp = ((b(0) & 0x1f) << 6) | (b(1) & 0x3f);
      i += 2;
    } else if (b(0) < 0xf0) {
      cp = ((b(0) & 0x0f) << 12) | ((b(1) & 0x3f) << 6) | (b(2) & 0x3f);
      i += 3;
    } else {
      cp = ((b(0) & 0x07) << 18) | ((b(1) & 0x3f) << 12) | ((b(2) & 0x3f) << 6) | (b(3) & 0x3f);
      i += 4;
    }
    if (cp >= 0x10000) {
      out.push_back(static_cast<char16_t>(0xd800 + ((cp - 0x10000) >> 10)));
      out.push_back(static_cast<char16_t>(0xdc00 + ((cp - 0x10000) & 0x3ff)));
    } else {
      out.push_back(static_cast<char16_t>(cp));
    }
  }
  return out;
}

void put_canonical(std::string& out, const Value& v) {
  if (const Array* a = v.if_array()) {
    out.push_back('[');
    for (std::size_t k = 0; k < a->size(); ++k) {
      if (k > 0) out.push_back(',');
      put_canonical(out, (*a)[k]);
    }
    out.push_back(']');
  } else if (const Object* o = v.if_object()) {
    std::vector<std::pair<std::u16string, const Member*>> sorted;
    sorted.reserve(o->size());
    for (const Member& m : *o) sorted.emplace_back(utf16(m.key), &m);
    std::ranges::sort(sorted, {}, &std::pair<std::u16string, const Member*>::first);
    out.push_back('{');
    for (std::size_t k = 0; k < sorted.size(); ++k) {
      if (k > 0) out.push_back(',');
      quote(out, sorted[k].second->key);
      out.push_back(':');
      put_canonical(out, sorted[k].second->value);
    }
    out.push_back('}');
  } else if (const std::string* s = v.if_string()) {
    quote(out, *s);
  } else if (const double* x = v.if_number()) {
    out += format_number(*x);
  } else if (const bool* b = v.if_bool()) {
    out += *b ? "true" : "false";
  } else {
    out += "null";
  }
}

void put_pretty(std::string& out, const Value& v, int indent) {
  const auto newline = [&](int level) {
    out.push_back('\n');
    out.append(static_cast<std::size_t>(2 * level), ' ');
  };
  if (const Array* a = v.if_array(); a && !a->empty()) {
    out.push_back('[');
    for (std::size_t k = 0; k < a->size(); ++k) {
      if (k > 0) out.push_back(',');
      newline(indent + 1);
      put_pretty(out, (*a)[k], indent + 1);
    }
    newline(indent);
    out.push_back(']');
  } else if (const Object* o = v.if_object(); o && !o->empty()) {
    out.push_back('{');
    for (std::size_t k = 0; k < o->size(); ++k) {
      if (k > 0) out.push_back(',');
      newline(indent + 1);
      quote(out, (*o)[k].key);
      out += ": ";
      put_pretty(out, (*o)[k].value, indent + 1);
    }
    newline(indent);
    out.push_back('}');
  } else {
    put_canonical(out, v);  // scalars and empty containers
  }
}

}  // namespace

std::expected<Value, ParseError> parse(std::string_view text) { return Parser(text).document(); }

std::string write(const Value& v) {
  std::string out;
  put_pretty(out, v, 0);
  out.push_back('\n');
  return out;
}

std::string canonical(const Value& v) {
  std::string out;
  put_canonical(out, v);
  return out;
}

std::string format_number(double x) {
  if (x == 0.0) return "0";
  // Shortest round-trip digits and exponent, then ECMAScript's layout (ECMA-262
  // Number::toString): k digits d, decimal point after n of them.
  std::array<char, 40> buf{};
  const auto r = std::to_chars(buf.data(), buf.data() + buf.size(), std::abs(x),
                               std::chars_format::scientific);
  const std::string_view sci(buf.data(), static_cast<std::size_t>(r.ptr - buf.data()));
  const std::size_t e_at = sci.find('e');
  std::string digits;
  for (const char c : sci.substr(0, e_at)) {
    if (c != '.') digits.push_back(c);
  }
  const int exponent = std::atoi(std::string(sci.substr(e_at + 1)).c_str());
  const int k = static_cast<int>(digits.size());
  const int n = exponent + 1;
  std::string out = x < 0 ? "-" : "";
  if (k <= n && n <= 21) {
    out += digits;
    out.append(static_cast<std::size_t>(n - k), '0');
  } else if (0 < n && n <= 21) {
    out += digits.substr(0, static_cast<std::size_t>(n));
    out += '.';
    out += digits.substr(static_cast<std::size_t>(n));
  } else if (-6 < n && n <= 0) {
    out += "0.";
    out.append(static_cast<std::size_t>(-n), '0');
    out += digits;
  } else {
    out += digits[0];
    if (k > 1) {
      out += '.';
      out += digits.substr(1);
    }
    out += exponent > 0 ? "e+" : "e-";
    out += std::to_string(std::abs(exponent));
  }
  return out;
}

}  // namespace nf::ir::json
