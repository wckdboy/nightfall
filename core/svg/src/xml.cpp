// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "nf/svg/xml.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>

namespace nf::svg::xml {

const std::string* Element::attribute(std::string_view attribute_name) const {
  for (const auto& [key, value] : attributes) {
    if (key == attribute_name) return &value;
  }
  return nullptr;
}

std::string_view Element::local_name() const {
  const std::string_view n = name;
  const std::size_t colon = n.find(':');
  return colon == std::string_view::npos ? n : n.substr(colon + 1);
}

namespace {

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
bool is_name_char(char c) {
  const auto u = static_cast<unsigned char>(c);
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
         c == '_' || c == ':' || c == '.' || u >= 0x80;
}

void append_utf8(std::string& out, std::uint32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

class Reader {
 public:
  explicit Reader(std::string_view text) : s_(text) {}

  std::expected<Element, ParseError> run() {
    if (s_.starts_with("\xEF\xBB\xBF")) pos_ = 3;
    if (!misc(true)) return failure();
    if (!at('<')) return fail("expected the root element"), failure();
    Element root;
    if (!element(root, 1)) return failure();
    if (!misc(false)) return failure();
    if (pos_ != s_.size()) return fail("content after the root element"), failure();
    return root;
  }

 private:
  // Whitespace, comments and processing instructions (and, before the root, one DOCTYPE).
  bool misc(bool prolog) {
    for (;;) {
      while (pos_ < s_.size() && is_space(s_[pos_])) ++pos_;
      if (starts("<!--")) {
        if (!skip_past("<!--", "-->")) return false;
      } else if (starts("<?")) {
        if (!skip_past("<?", "?>")) return false;
      } else if (prolog && starts("<!DOCTYPE")) {
        if (!doctype()) return false;
      } else {
        return true;
      }
    }
  }

  bool doctype() {
    int brackets = 0;
    for (pos_ += 9; pos_ < s_.size(); ++pos_) {
      const char c = s_[pos_];
      if (c == '[') ++brackets;
      if (c == ']') --brackets;
      if (c == '>' && brackets <= 0) {
        ++pos_;
        return true;
      }
    }
    return fail("unterminated DOCTYPE"), false;
  }

  bool element(Element& e, int depth) {
    if (depth > kMaxDepth) return fail("elements nested too deeply"), false;
    e.offset = pos_++;  // '<'
    if (!name(e.name)) return false;
    for (;;) {
      const bool spaced = skip_space();
      if (at('/')) {
        ++pos_;
        if (!at('>')) return fail("expected '>'"), false;
        ++pos_;
        return true;
      }
      if (at('>')) {
        ++pos_;
        break;
      }
      if (!spaced) return fail("expected whitespace before an attribute"), false;
      if (!attribute(e)) return false;
    }
    return content(e, depth);
  }

  bool attribute(Element& e) {
    std::string key, value;
    const std::size_t at_key = pos_;
    if (!name(key)) return false;
    skip_space();
    if (!at('=')) return fail("expected '=' after an attribute name"), false;
    ++pos_;
    skip_space();
    if (!at('"') && !at('\'')) return fail("expected a quoted attribute value"), false;
    const char quote = s_[pos_++];
    for (;;) {
      if (pos_ >= s_.size()) return fail("unterminated attribute value"), false;
      const char c = s_[pos_];
      if (c == quote) break;
      if (c == '<') return fail("'<' in an attribute value"), false;
      if (c == '&') {
        if (!entity(value)) return false;
        continue;
      }
      // Line-end then attribute-value normalisation: CR LF, CR, LF and tab become a space.
      if (c == '\r' && pos_ + 1 < s_.size() && s_[pos_ + 1] == '\n') ++pos_;
      value += is_space(c) ? ' ' : c;
      ++pos_;
    }
    ++pos_;
    if (e.attribute(key) != nullptr) {
      pos_ = at_key;
      return fail("duplicate attribute"), false;
    }
    e.attributes.emplace_back(std::move(key), std::move(value));
    return true;
  }

  // Children, comments, CDATA and text (discarded) up to the matching end tag.
  bool content(Element& e, int depth) {
    for (;;) {
      if (pos_ >= s_.size()) return fail("unterminated element <" + e.name + ">"), false;
      if (starts("</")) {
        pos_ += 2;
        std::string end;
        const std::size_t at_end = pos_;
        if (!name(end)) return false;
        if (end != e.name) {
          pos_ = at_end;
          return fail("end tag does not match <" + e.name + ">"), false;
        }
        skip_space();
        if (!at('>')) return fail("expected '>'"), false;
        ++pos_;
        return true;
      }
      if (starts("<!--")) {
        if (!skip_past("<!--", "-->")) return false;
      } else if (starts("<![CDATA[")) {
        if (!skip_past("<![CDATA[", "]]>")) return false;
      } else if (starts("<?")) {
        if (!skip_past("<?", "?>")) return false;
      } else if (at('<')) {
        e.children.emplace_back();
        if (!element(e.children.back(), depth + 1)) return false;
      } else if (at('&')) {
        std::string ignored;  // text is discarded, but its entities must be well formed
        if (!entity(ignored)) return false;
      } else {
        ++pos_;
      }
    }
  }

  bool entity(std::string& out) {
    const std::size_t start = pos_;
    const std::size_t semi = s_.find(';', pos_);
    if (semi == std::string_view::npos || semi - pos_ > 12) {
      return fail("malformed entity reference"), false;
    }
    const std::string_view ref = s_.substr(pos_ + 1, semi - pos_ - 1);
    pos_ = semi + 1;
    if (ref == "lt") return out += '<', true;
    if (ref == "gt") return out += '>', true;
    if (ref == "amp") return out += '&', true;
    if (ref == "quot") return out += '"', true;
    if (ref == "apos") return out += '\'', true;
    if (ref.size() >= 2 && ref[0] == '#') {
      const bool hex = ref[1] == 'x';
      const std::string_view digits = ref.substr(hex ? 2 : 1);
      std::uint32_t cp = 0;
      bool ok = !digits.empty();
      for (const char c : digits) {
        int d = -1;
        if (c >= '0' && c <= '9') d = c - '0';
        if (hex && c >= 'a' && c <= 'f') d = c - 'a' + 10;
        if (hex && c >= 'A' && c <= 'F') d = c - 'A' + 10;
        if (d < 0 || cp > 0x10FFFF) {
          ok = false;
          break;
        }
        cp = cp * (hex ? 16u : 10u) + static_cast<std::uint32_t>(d);
      }
      if (ok && cp != 0 && cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF)) {
        append_utf8(out, cp);
        return true;
      }
    }
    pos_ = start;
    return fail("unknown or invalid entity"), false;
  }

  bool name(std::string& out) {
    const std::size_t start = pos_;
    while (pos_ < s_.size() && is_name_char(s_[pos_])) ++pos_;
    if (pos_ == start) return fail("expected a name"), false;
    out.assign(s_.substr(start, pos_ - start));
    return true;
  }

  bool skip_past(std::string_view open, std::string_view close) {
    const std::size_t end = s_.find(close, pos_ + open.size());
    if (end == std::string_view::npos) return fail("unterminated " + std::string(open)), false;
    pos_ = end + close.size();
    return true;
  }

  bool skip_space() {
    const std::size_t start = pos_;
    while (pos_ < s_.size() && is_space(s_[pos_])) ++pos_;
    return pos_ > start;
  }

  bool at(char c) const { return pos_ < s_.size() && s_[pos_] == c; }
  bool starts(std::string_view p) const { return s_.substr(pos_).starts_with(p); }

  void fail(std::string message) {
    if (!error_) error_ = ParseError{pos_, std::move(message)};
  }
  std::unexpected<ParseError> failure() { return std::unexpected(*error_); }

  std::string_view s_;
  std::size_t pos_ = 0;
  std::optional<ParseError> error_;
};

}  // namespace

std::expected<Element, ParseError> parse(std::string_view text) { return Reader(text).run(); }

}  // namespace nf::svg::xml
