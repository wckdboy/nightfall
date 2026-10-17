// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "nf/math/expression.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <numbers>

#include <xlocale.h>

#include "nf/math/knot_vector.hpp"

namespace nf::math {

struct Expression::Node {
  using Kind = Instruction::Kind;
  Kind kind;
  double value = 0.0;
  std::string func;
  std::shared_ptr<const Node> a, b;
  std::size_t offset = 0, depth = 1;
};

namespace {

using Node = Expression::Node;
using NodePtr = std::shared_ptr<const Node>;

constexpr std::array<std::string_view, 13> kFunctions{"sin",  "cos",  "tan",  "asin", "acos",
                                                      "atan", "sinh", "cosh", "tanh", "exp",
                                                      "log",  "sqrt", "abs"};

// expr := term (('+'|'-') term)*; term := unary (('*'|'/') unary)*;
// unary := ('+'|'-') unary | power; power := atom ('^' unary)?; atom := number | name | '(' expr
// ')'
class Parser {
 public:
  explicit Parser(std::string_view s, bool named = false) : s_(s), named_(named) {}
  std::expected<NodePtr, Expression::ParseError> run() {
    if (s_.size() > 65536) {
      fail("expression source limit exceeded");
      return std::unexpected(*error_);
    }
    NodePtr e = expr();
    space();
    if (!error_ && pos_ != s_.size()) fail("unexpected '" + std::string(1, s_[pos_]) + "'");
    if (error_) return std::unexpected(*error_);
    return e;
  }

 private:
  NodePtr make(Node::Kind k, NodePtr a = {}, NodePtr b = {}, std::size_t at = 0, double value = 0,
               std::string name = {}) {
    if (error_) return {};
    const auto depth = 1 + std::max(a ? a->depth : 0, b ? b->depth : 0);
    if (++nodes_ > 4096 || depth > 128) {
      fail("expression complexity limit exceeded");
      return {};
    }
    return std::make_shared<const Node>(
        Node{k, value, std::move(name), std::move(a), std::move(b), at, depth});
  }
  NodePtr expr() {
    NodePtr left = term();
    for (space(); !error_ && (peek('+') || peek('-')); space()) {
      const auto at = pos_++;
      left = make(s_[at] == '+' ? Node::Kind::kAdd : Node::Kind::kSub, left, term(), at);
    }
    return left;
  }
  NodePtr term() {
    NodePtr left = unary();
    for (space(); !error_ && (peek('*') || peek('/')); space()) {
      const auto at = pos_++;
      left = make(s_[at] == '*' ? Node::Kind::kMul : Node::Kind::kDiv, left, unary(), at);
    }
    return left;
  }
  NodePtr unary() {
    struct Guard {
      std::size_t& depth;
      ~Guard() { --depth; }
    } guard{depth_};
    if (++depth_ > 128) return fail("expression recursion limit exceeded"), nullptr;
    space();
    if (peek('-')) {
      const auto at = pos_++;
      return make(Node::Kind::kNeg, unary(), {}, at);
    }
    if (peek('+')) return ++pos_, unary();
    NodePtr base = atom();
    space();
    if (!error_ && peek('^')) {
      const auto at = pos_++;
      return make(Node::Kind::kPow, base, unary(), at);
    }
    return base;
  }
  NodePtr atom() {
    space();
    if (error_) return {};
    if (peek('(')) {
      ++pos_;
      NodePtr e = expr();
      space();
      if (!peek(')')) return fail("expected ')'"), nullptr;
      ++pos_;
      return e;
    }
    if (pos_ < s_.size() &&
        (std::isdigit(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '.')) {
      const auto at = pos_;
      static const locale_t c_locale = newlocale(LC_ALL_MASK, "C", nullptr);
      const std::string rest(s_.substr(pos_));
      char* stop = nullptr;
      const double v = strtod_l(rest.c_str(), &stop, c_locale);
      const std::string_view token(rest.c_str(), static_cast<std::size_t>(stop - rest.c_str()));
      if (token.empty() || !std::isfinite(v) ||
          token.find_first_not_of("0123456789.eE+-") != std::string_view::npos) {
        return fail("invalid number"), nullptr;
      }
      pos_ += static_cast<std::size_t>(stop - rest.c_str());
      space();
      const auto suffix = named_ ? identifier() : std::string_view{};
      return make(Node::Kind::kNum, {}, {}, at, v, std::string(suffix));
    }
    const std::size_t start = pos_;
    const auto name = identifier();
    space();
    if (!named_ && name == "t") return make(Node::Kind::kName, {}, {}, start, 0, "t");
    if (!named_ && (name == "pi" || name == "e"))
      return make(Node::Kind::kNum, {}, {}, start,
                  name == "pi" ? std::numbers::pi : std::numbers::e);
    if (std::ranges::contains(kFunctions, name) && (!named_ || peek('('))) {
      if (!peek('(')) return fail("expected '(' after " + std::string(name)), nullptr;
      ++pos_;
      NodePtr arg = expr();
      space();
      if (!peek(')')) return fail("expected ')'"), nullptr;
      ++pos_;
      return make(Node::Kind::kFunc, arg, {}, start, 0, std::string(name));
    }
    if (named_ && !name.empty() && !peek('('))
      return make(Node::Kind::kName, {}, {}, start, 0, std::string(name));
    pos_ = start;
    return fail(name.empty() ? "expected a value" : "unknown name '" + std::string(name) + "'"),
           nullptr;
  }
  std::string_view identifier() {
    const auto start = pos_;
    const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || c == '_'; };
    if (pos_ < s_.size() && letter(s_[pos_])) {
      do {
        ++pos_;
      } while (pos_ < s_.size() && (letter(s_[pos_]) || (s_[pos_] >= '0' && s_[pos_] <= '9')));
    }
    if (pos_ - start > 64) fail("identifier length limit exceeded");
    return s_.substr(start, pos_ - start);
  }
  void space() {
    while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_]))) ++pos_;
  }
  bool peek(char c) const { return pos_ < s_.size() && s_[pos_] == c; }
  void fail(std::string m) {
    if (!error_) error_ = Expression::ParseError{std::move(m), pos_};
  }

  std::string_view s_;
  bool named_;
  std::size_t pos_ = 0, depth_ = 0, nodes_ = 0;
  std::optional<Expression::ParseError> error_;
};

double call(std::string_view f, double x) {
  if (f == "sin") return std::sin(x);
  if (f == "cos") return std::cos(x);
  if (f == "tan") return std::tan(x);
  if (f == "asin") return std::asin(x);
  if (f == "acos") return std::acos(x);
  if (f == "atan") return std::atan(x);
  if (f == "sinh") return std::sinh(x);
  if (f == "cosh") return std::cosh(x);
  if (f == "tanh") return std::tanh(x);
  if (f == "exp") return std::exp(x);
  if (f == "log") return std::log(x);
  if (f == "sqrt") return std::sqrt(x);
  return f == "abs" ? std::abs(x) : NAN;
}

double eval(const Node& n, double t) {
  using K = Node::Kind;
  switch (n.kind) {
    case K::kNum:
      return n.value;
    case K::kName:
      return t;
    case K::kAdd:
      return eval(*n.a, t) + eval(*n.b, t);
    case K::kSub:
      return eval(*n.a, t) - eval(*n.b, t);
    case K::kMul:
      return eval(*n.a, t) * eval(*n.b, t);
    case K::kDiv:
      return eval(*n.a, t) / eval(*n.b, t);
    case K::kPow:
      return std::pow(eval(*n.a, t), eval(*n.b, t));
    case K::kNeg:
      return -eval(*n.a, t);
    case K::kFunc:
      return call(n.func, eval(*n.a, t));
  }
  return NAN;
}

using Poly = std::vector<double>;  // coefficients of t^0, t^1, …

std::optional<Poly> poly_raw(const Node& n);

// Intermediate polynomials may exceed the final degree limit (t^10 − t^10 is 0), up to this.
constexpr std::size_t kIntermediateDegree = 64;

// The expression as a polynomial in t, if it is one, without trailing zero coefficients
// (t·0 is the constant 0). The final degree limit is applied by Expression::polynomial().
std::optional<Poly> poly(const Node& n) {
  auto p = poly_raw(n);
  if (p) {
    while (p->size() > 1 && p->back() == 0.0) p->pop_back();
  }
  return p;
}

std::optional<Poly> poly_raw(const Node& n) {
  using K = Node::Kind;
  const auto combine = [](Poly a, const Poly& b, double sign) {
    a.resize(std::max(a.size(), b.size()), 0.0);
    for (std::size_t i = 0; i < b.size(); ++i) a[i] += sign * b[i];
    return a;
  };
  const auto times = [](const Poly& a, const Poly& b) -> std::optional<Poly> {
    if (a.size() + b.size() - 1 > kIntermediateDegree + 1) return std::nullopt;
    Poly r(a.size() + b.size() - 1, 0.0);
    for (std::size_t i = 0; i < a.size(); ++i) {
      for (std::size_t j = 0; j < b.size(); ++j) r[i + j] += a[i] * b[j];
    }
    return r;
  };
  switch (n.kind) {
    case K::kNum:
      return Poly{n.value};
    case K::kName:
      return Poly{0.0, 1.0};
    case K::kNeg: {
      auto a = poly(*n.a);
      if (a) std::ranges::for_each(*a, [](double& c) { c = -c; });
      return a;
    }
    case K::kAdd:
    case K::kSub: {
      auto a = poly(*n.a), b = poly(*n.b);
      if (!a || !b) return std::nullopt;
      return combine(*a, *b, n.kind == K::kAdd ? 1.0 : -1.0);
    }
    case K::kMul: {
      auto a = poly(*n.a), b = poly(*n.b);
      if (!a || !b) return std::nullopt;
      return times(*a, *b);
    }
    case K::kDiv: {
      auto a = poly(*n.a), b = poly(*n.b);
      if (!a || !b || b->size() != 1 || (*b)[0] == 0.0) return std::nullopt;
      for (double& c : *a) c /= (*b)[0];
      return a;
    }
    case K::kPow: {
      auto base = poly(*n.a), ex = poly(*n.b);
      if (!base || !ex || ex->size() != 1) return std::nullopt;
      const double k = (*ex)[0];
      if (base->size() == 1) return Poly{std::pow((*base)[0], k)};
      if (k < 0 || k > static_cast<double>(kIntermediateDegree) || k != std::floor(k))
        return std::nullopt;
      Poly r{1.0};
      for (int i = 0; i < static_cast<int>(k); ++i) {
        auto next = times(r, *base);
        if (!next) return std::nullopt;
        r = std::move(*next);
      }
      return r;
    }
    case K::kFunc: {
      auto a = poly(*n.a);
      if (!a || a->size() != 1) return std::nullopt;
      return Poly{call(n.func, (*a)[0])};
    }
  }
  return std::nullopt;
}

using Trig = Expression::Sinusoid;

std::optional<Trig> trig_raw(const Node& n);

// trig_raw, normalised: a zero amplitude is a constant (no frequency), and frequencies are
// positive (cos(−x) = cos x, sin(−x) = −sin x), so equal frequencies compare equal.
std::optional<Trig> trig(const Node& n) {
  auto r = trig_raw(n);
  if (r && r->b == 0.0 && r->c == 0.0) r->omega.reset();
  if (r && r->omega && *r->omega < 0) {
    r->omega = -*r->omega;
    r->c = -r->c;
  }
  return r;
}

std::optional<Trig> trig_raw(const Node& n) {
  using K = Node::Kind;
  if (auto p = poly(n); p && p->size() == 1) return Trig{(*p)[0], 0, 0, std::nullopt};
  switch (n.kind) {
    case K::kFunc: {
      if (n.func != "sin" && n.func != "cos") return std::nullopt;
      auto arg = poly(*n.a);
      if (!arg || arg->size() != 2 || (*arg)[1] == 0.0) return std::nullopt;
      const double phi = (*arg)[0], omega = (*arg)[1];
      // cos(ωt+φ) = cos φ cos ωt − sin φ sin ωt; sin(ωt+φ) = sin φ cos ωt + cos φ sin ωt.
      if (n.func == "cos") return Trig{0, std::cos(phi), -std::sin(phi), omega};
      return Trig{0, std::sin(phi), std::cos(phi), omega};
    }
    case K::kNeg: {
      auto x = trig(*n.a);
      if (x) *x = {-x->a, -x->b, -x->c, x->omega};
      return x;
    }
    case K::kAdd:
    case K::kSub: {
      auto x = trig(*n.a), y = trig(*n.b);
      if (!x || !y) return std::nullopt;
      if (x->omega && y->omega && *x->omega != *y->omega) return std::nullopt;
      const double s = n.kind == K::kAdd ? 1.0 : -1.0;
      return Trig{x->a + s * y->a, x->b + s * y->b, x->c + s * y->c,
                  x->omega ? x->omega : y->omega};
    }
    case K::kMul: {  // an oscillation times a constant
      auto x = trig(*n.a), y = trig(*n.b);
      if (!x || !y || (x->omega && y->omega)) return std::nullopt;
      const Trig& f = x->omega ? *x : *y;
      const double k = x->omega ? y->a : x->a;
      return Trig{f.a * k, f.b * k, f.c * k, f.omega};
    }
    case K::kDiv: {  // an oscillation over a non-zero constant
      auto x = trig(*n.a), y = trig(*n.b);
      if (!x || !y || y->omega || y->a == 0.0) return std::nullopt;
      return Trig{x->a / y->a, x->b / y->a, x->c / y->a, x->omega};
    }
    default:
      return std::nullopt;
  }
}

}  // namespace

std::expected<Expression, std::string> Expression::parse(std::string_view text) {
  auto root = Parser(text).run();
  if (!root)
    return std::unexpected(root.error().message + " at " + std::to_string(root.error().offset));
  return Expression(std::move(*root));
}

std::expected<Expression::Program, Expression::ParseError> Expression::compile_named(
    std::string_view text) {
  const auto root = Parser(text, true).run();
  if (!root) return std::unexpected(root.error());
  Program program;
  const auto append = [&](const auto& self, const Node& n) -> void {
    if (n.a) self(self, *n.a);
    if (n.b) self(self, *n.b);
    program.push_back({n.kind, n.value, n.func, n.offset});
  };
  append(append, **root);
  return program;
}

double Expression::apply_function(std::string_view name, double value) { return call(name, value); }

double Expression::operator()(double t) const { return eval(*root_, t); }

std::optional<std::vector<double>> Expression::polynomial() const {
  auto p = poly(*root_);
  if (p && p->size() > static_cast<std::size_t>(KnotVector::kMaxDegree) + 1) return std::nullopt;
  return p;
}

std::optional<Expression::Sinusoid> Expression::sinusoid() const { return trig(*root_); }

}  // namespace nf::math
