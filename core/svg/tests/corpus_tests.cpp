// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// Phase 1 gate (docs/phases/phase-1.md): 200 generated SVG documents round-trip. Each mixes
// every path command (absolute and relative, varied number syntax), every basic shape, nested
// groups with every transform function, and different units, viewBoxes and aspect modes.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "nf/math/refine.hpp"
#include "nf/svg/document.hpp"
#include "nf/svg/export.hpp"

using nf::math::NurbsCurve2;
using nf::math::Vec2;
using namespace nf::svg;

namespace {

class Generator {
 public:
  explicit Generator(std::uint32_t seed) : rng_(seed) {}

  std::string document() {
    std::string root;
    switch (pick(4)) {
      case 0:
        root = R"(width="210mm" height="297mm" viewBox="0 0 210 297")";
        break;
      case 1:
        root = R"(width="800" height="600")";
        break;
      case 2:
        root = R"(width="12cm" height="9cm" viewBox="-40 -30 400 300")";
        break;
      default:
        root = R"(width="5in" height="3in" viewBox="0 0 500 300" preserveAspectRatio="none")";
    }
    std::string body;
    const int n = 3 + pick(6);
    for (int i = 0; i < n; ++i) body += element(0);
    return R"(<svg xmlns="http://www.w3.org/2000/svg" )" + root + ">" + body + "</svg>";
  }

 private:
  std::string element(int depth) {
    switch (depth < 2 ? pick(9) : 1 + pick(8)) {
      case 0: {
        std::string inner;
        for (int i = 1 + pick(3); i > 0; --i) inner += element(depth + 1);
        return R"(<g transform=")" + transform() + R"(">)" + inner + "</g>";
      }
      case 1:
      case 2:
      case 3:
        return R"(<path d=")" + path() + R"("/>)";
      case 4:
        return "<rect x=\"" + num() + "\" y=\"" + num() + "\" width=\"" + pos() + "\" height=\"" +
               pos() + (pick(2) ? "\" rx=\"" + pos(5) : std::string()) + "\"/>";
      case 5:
        return "<circle cx=\"" + num() + "\" cy=\"" + num() + "\" r=\"" + pos() + "\"/>";
      case 6:
        return "<ellipse cx=\"" + num() + "\" cy=\"" + num() + "\" rx=\"" + pos() + "\" ry=\"" +
               pos() + "\"/>";
      case 7:
        return "<line x1=\"" + num() + "\" y1=\"" + num() + "\" x2=\"" + num() + "\" y2=\"" +
               num() + "\"/>";
      default: {
        std::string pts;
        for (int i = 3 + pick(5); i > 0; --i) pts += num() + (pick(2) ? "," : " ") + num() + " ";
        return std::string(pick(2) ? "<polyline" : "<polygon") + " points=\"" + pts + "\"/>";
      }
    }
  }

  std::string path() {
    std::string d = "M" + num() + " " + num();
    bool after_z = false;
    for (int i = 2 + pick(9); i > 0; --i) {
      const std::string cmds = "LHVCSQTAZ";
      char c = cmds[static_cast<std::size_t>(pick(9))];
      if (after_z && c == 'Z') c = 'L';
      after_z = c == 'Z';
      const bool rel = pick(2) == 0 && c != 'Z';
      d += rel ? static_cast<char>(c - 'A' + 'a') : c;
      const auto coord = [&] { return rel ? num(30) : num(); };
      switch (c) {
        case 'L':
        case 'T':
          d += coord() + " " + coord();
          break;
        case 'H':
        case 'V':
          d += coord();
          break;
        case 'C':
          for (int k = 0; k < 6; ++k) d += coord() + " ";
          break;
        case 'S':
        case 'Q':
          for (int k = 0; k < 4; ++k) d += coord() + " ";
          break;
        case 'A':
          d += pos() + " " + pos() + " " + num(180) + " " + std::to_string(pick(2)) + " " +
               std::to_string(pick(2)) + " " + coord() + " " + coord();
          break;
        default:
          break;
      }
    }
    return d;
  }

  std::string transform() {
    std::string t;
    for (int i = 1 + pick(3); i > 0; --i) {
      switch (pick(6)) {
        case 0:
          t += "translate(" + num() + "," + num() + ") ";
          break;
        case 1:
          t += "scale(" + pos(3) + (pick(2) ? " " + pos(3) : std::string()) + ") ";
          break;
        case 2:
          t += "rotate(" + num(180) + (pick(2) ? " " + num() + " " + num() : std::string()) + ") ";
          break;
        case 3:
          t += "skewX(" + num(40) + ") ";
          break;
        case 4:
          t += "skewY(" + num(40) + ") ";
          break;
        default:
          t += "matrix(" + pos(2) + " " + num(1) + " " + num(1) + " " + pos(2) + " " + num() + " " +
               num() + ") ";
      }
    }
    return t;
  }

  int pick(int n) { return static_cast<int>(rng_() % static_cast<std::uint32_t>(n)); }
  // Numbers in varied syntax: integers, decimals, exponents.
  std::string num(double range = 100) {
    std::uniform_real_distribution<double> d(-range, range);
    char buf[48];
    const double v = d(rng_);
    switch (pick(3)) {
      case 0:
        std::snprintf(buf, sizeof buf, "%d", static_cast<int>(v));
        break;
      case 1:
        std::snprintf(buf, sizeof buf, "%.4f", v);
        break;
      default:
        std::snprintf(buf, sizeof buf, "%.6e", v);
    }
    return buf;
  }
  std::string pos(double range = 60) {
    std::uniform_real_distribution<double> d(0.5, range);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.5g", d(rng_));
    return buf;
  }

  std::mt19937 rng_;
};

// Every Bézier span of a drawing, in order.
std::vector<NurbsCurve2> spans(const Drawing& d) {
  std::vector<NurbsCurve2> out;
  for (const Subpath& sp : d.paths) {
    for (const NurbsCurve2& c : sp.segments) {
      for (NurbsCurve2& s : nf::math::decompose(c)) out.push_back(std::move(s));
    }
  }
  return out;
}

}  // namespace

TEST_CASE("Phase 1 gate: 200 SVG documents round-trip", "[svg][corpus][gate]") {
  int documents = 0, total_spans = 0, rational_spans = 0;
  for (std::uint32_t seed = 1; seed <= 200; ++seed) {
    const std::string svg = Generator(seed).document();
    INFO("document " << seed << ": " << svg);
    const auto first = read_document(svg);
    REQUIRE(first.has_value());
    const auto written = write_document(first->paths);
    REQUIRE(written.has_value());
    CHECK(written->max_error == 0.0);  // every SVG command is exact
    const auto second = read_document(written->text);
    REQUIRE(second.has_value());
    CHECK(second->warnings.empty());

    const auto a = spans(*first), b = spans(*second);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
      REQUIRE(a[i].degree() == b[i].degree());
      REQUIRE(a[i].points().size() == b[i].points().size());
      double scale = 1.0;
      for (const Vec2 p : a[i].points()) scale = std::max({scale, std::abs(p.x), std::abs(p.y)});
      if (a[i].rational()) {
        // Arcs come back through A commands: same geometry, rebuilt from centre and radii.
        ++rational_spans;
        for (std::size_t k = 0; k < a[i].points().size(); ++k) {
          CHECK(length(a[i].points()[k] - b[i].points()[k]) <= 1e-12 * scale);
        }
        for (int s = 0; s <= 8; ++s) {
          const double u = s / 8.0;
          const Vec2 pa =
              a[i].point(a[i].domain_start() + (a[i].domain_end() - a[i].domain_start()) * u);
          const Vec2 pb =
              b[i].point(b[i].domain_start() + (b[i].domain_end() - b[i].domain_start()) * u);
          CHECK(length(pa - pb) <= 1e-12 * scale);
        }
      } else {
        CHECK(std::ranges::equal(a[i].points(), b[i].points()));  // bit for bit
      }
    }
    total_spans += static_cast<int>(a.size());
    ++documents;
  }
  CHECK(documents == 200);
  CHECK(total_spans > 2000);    // the corpus is not trivially small
  CHECK(rational_spans > 200);  // and arcs are well represented
}
