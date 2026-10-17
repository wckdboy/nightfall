// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "nf/ir/json.hpp"

#include <algorithm>
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

  // The code point of the UTF-8 sequence at i_, validated (no overlongs, surrogates or
  // > U+10FFFF), and advances past it.
  std::optional<unsigned> utf8() {
    const auto byte = [&](std::size_t k) { return static_cast<unsigned char>(s_[i_ + k]); };
    const unsigned char b0 = byte(0);
    std::size_t len = 0;
    unsigned lo = 0x80, hi = 0xbf;  // allowed range of the second byte
    unsigned cp = 0;
    if (b0 >= 0xc2 && b0 <= 0xdf) {
      len = 2;
      cp = b0 & 0x1fu;
    } else if (b0 >= 0xe0 && b0 <= 0xef) {
      len = 3;
      cp = b0 & 0x0fu;
      if (b0 == 0xe0) lo = 0xa0;
      if (b0 == 0xed) hi = 0x9f;  // excludes the surrogates
    } else if (b0 >= 0xf0 && b0 <= 0xf4) {
      len = 4;
      cp = b0 & 0x07u;
      if (b0 == 0xf0) lo = 0x90;
      if (b0 == 0xf4) hi = 0x8f;
    } else {
      return std::nullopt;
    }
    if (i_ + len > s_.size()) return std::nullopt;
    if (byte(1) < lo || byte(1) > hi) return std::nullopt;
    for (std::size_t k = 1; k < len; ++k) {
      if (byte(k) < 0x80 || byte(k) > 0xbf) return std::nullopt;
      cp = (cp << 6) | (byte(k) & 0x3fu);
    }
    i_ += len;
    return cp;
  }

  // I-JSON (RFC 7493 §2.1) excludes the noncharacters U+FDD0…U+FDEF and U+xxFFFE/U+xxFFFF.
  static bool noncharacter(unsigned cp) {
    return (cp >= 0xfdd0 && cp <= 0xfdef) || (cp & 0xfffe) == 0xfffe;
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
        const std::size_t at = i_;
        const auto cp = utf8();
        if (!cp) return fail_at(at, "invalid UTF-8");
        if (noncharacter(*cp)) return fail_at(at, "noncharacter in a string");
        put(out, *cp);
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
          if (noncharacter(cp)) return fail_at(at, "noncharacter in a string");
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
}  // namespace

std::expected<Value, ParseError> parse(std::string_view text) { return Parser(text).document(); }

}  // namespace nf::ir::json
