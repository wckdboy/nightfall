// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "nf/math/arrangement.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>

#include "nf/math/constructors.hpp"
#include "nf/math/intersect.hpp"
#include "nf/math/query.hpp"
#include "nf/math/refine.hpp"

namespace nf::math {

namespace {

std::size_t at(int i) { return static_cast<std::size_t>(i); }

Vec2 start_of(const NurbsCurve2& c) { return c.points().front(); }
Vec2 end_of(const NurbsCurve2& c) { return c.points().back(); }

// Direction leaving the curve's start: the tangent, or, where it vanishes, the chord to a
// nearby point.
Vec2 leaving(const NurbsCurve2& c) {
  std::array<Vec2, 2> d{};
  c.derivatives(c.domain_start(), 1, d);
  if (length(d[1]) > 0) return d[1] / length(d[1]);
  const Vec2 q = c.point(c.domain_start() + 1e-6 * (c.domain_end() - c.domain_start())) - d[0];
  return q / std::max(length(q), 1e-300);
}

// Diagonal of the control points' box.
double extent(const NurbsCurve2& c) {
  Vec2 lo = c.points().front(), hi = lo;
  for (const Vec2 q : c.points()) {
    lo = {std::min(lo.x, q.x), std::min(lo.y, q.y)};
    hi = {std::max(hi.x, q.x), std::max(hi.y, q.y)};
  }
  return length(hi - lo);
}

// Shoelace over a fine polyline: twice the signed area swept with the origin.
double swept_area(const NurbsCurve2& c, double tol) {
  const auto pl = polyline(c, tol);
  double s = 0.0;
  for (std::size_t i = 1; i < pl.size(); ++i) s += cross(pl[i - 1].point, pl[i].point);
  return s;
}

// Winding number of the input around p: signed crossings of a ray from p. Rays that graze
// a vertex or touch a curve tangentially are ambiguous; another direction is tried.
std::optional<int> winding_at(const std::vector<NurbsCurve2>& input, Vec2 p, double tol) {
  for (int attempt = 0; attempt < 16; ++attempt) {
    const double angle = 0.7234 + attempt * 0.3979;  // arbitrary, irrational-ish directions
    const Vec2 dir{std::cos(angle), std::sin(angle)};
    int w = 0;
    bool ambiguous = false;
    for (const NurbsCurve2& c : input) {
      for (const auto& h : intersect_line(c, p, dir).hits) {
        if (dot(h.point - p, dir) <= 0) continue;  // behind the ray's start
        std::array<Vec2, 2> d{};
        c.derivatives(h.u, 1, d);
        const double turn = cross(dir, d[1]);
        const bool at_end =
            length(h.point - start_of(c)) <= tol || length(h.point - end_of(c)) <= tol;
        if (at_end || std::abs(turn) <= 1e-9 * length(d[1])) {
          ambiguous = true;
          break;
        }
        w += turn > 0 ? 1 : -1;
      }
      if (ambiguous) break;
    }
    if (!ambiguous) return w;
  }
  return std::nullopt;
}

}  // namespace

std::expected<Arrangement, std::string> arrange(std::span<const std::vector<NurbsCurve2>> paths) {
  // 1. The input curves, each path closed.
  std::vector<NurbsCurve2> input;
  Vec2 lo{INFINITY, INFINITY}, hi{-INFINITY, -INFINITY};
  for (const auto& path : paths) {
    if (path.empty()) continue;
    for (const NurbsCurve2& c : path) {
      input.push_back(c);
      for (const Vec2 q : c.points()) {
        lo = {std::min(lo.x, q.x), std::min(lo.y, q.y)};
        hi = {std::max(hi.x, q.x), std::max(hi.y, q.y)};
      }
    }
    if (end_of(path.back()) != start_of(path.front())) {
      input.push_back(make_line(end_of(path.back()), start_of(path.front())));
    }
  }
  Arrangement out;
  if (input.empty()) return out;
  const double size = std::max(length(hi - lo), 1e-300);
  const double tol = 1e-9 * size;

  // 2. Split parameters where curves meet (crossings, touches, T-junctions).
  std::vector<std::vector<double>> cuts(input.size());
  for (std::size_t i = 0; i < input.size(); ++i) {
    for (std::size_t j = i + 1; j < input.size(); ++j) {
      const auto r = intersect(input[i], input[j]);
      if (r.overlap)
        return std::unexpected(
            "curves share a stretch; coincident edges are not supported yet (#74)");
      for (const auto& h : r.hits) {
        const auto interior = [&](const NurbsCurve2& c, double u) {
          return length(c.point(u) - start_of(c)) > tol && length(c.point(u) - end_of(c)) > tol;
        };
        if (interior(input[i], h.ua)) cuts[i].push_back(h.ua);
        if (interior(input[j], h.ub)) cuts[j].push_back(h.ub);
      }
    }
  }

  // 3. Edges: the pieces between cuts, endpoints snapped to shared vertices.
  const auto vertex = [&](Vec2 p) {
    for (std::size_t v = 0; v < out.vertices.size(); ++v) {
      if (length(out.vertices[v] - p) <= tol) return static_cast<int>(v);
    }
    out.vertices.push_back(p);
    return static_cast<int>(out.vertices.size()) - 1;
  };
  for (std::size_t i = 0; i < input.size(); ++i) {
    std::ranges::sort(cuts[i]);
    NurbsCurve2 rest = input[i];
    std::vector<NurbsCurve2> pieces;
    for (const double u : cuts[i]) {
      if (!(u > rest.domain_start() && u < rest.domain_end())) continue;
      if (length(rest.point(u) - start_of(rest)) <= tol) continue;  // same point as the last cut
      auto halves = split(rest, u);
      if (!halves) continue;
      pieces.push_back(std::move(halves->first));
      rest = std::move(halves->second);
    }
    pieces.push_back(std::move(rest));
    for (NurbsCurve2& piece : pieces) {
      const int a = vertex(start_of(piece)), b = vertex(end_of(piece));
      // A piece with both ends at one vertex is kept only if it is a real loop.
      if (a == b && extent(piece) <= tol) continue;
      const int f = static_cast<int>(out.half_edges.size());
      out.half_edges.push_back({piece, a, b, f + 1, -1, -1, +1});
      out.half_edges.push_back({reverse(piece), b, a, f, -1, -1, -1});
    }
  }

  // 4. Around each vertex, outgoing half-edges by angle; next(h) is the one just clockwise of
  //    twin(h) at h's target, which keeps every face on the left.
  std::vector<std::vector<int>> around(out.vertices.size());
  std::vector<double> angle(out.half_edges.size());
  for (std::size_t h = 0; h < out.half_edges.size(); ++h) {
    const Vec2 t = leaving(out.half_edges[h].curve);
    angle[h] = std::atan2(t.y, t.x);
    around[at(out.half_edges[h].origin)].push_back(static_cast<int>(h));
  }
  for (auto& list : around) std::ranges::sort(list, {}, [&](int h) { return angle[at(h)]; });
  for (auto& he : out.half_edges) {
    const auto& list = around[at(he.target)];
    const auto k = std::ranges::find(list, he.twin) - list.begin();
    he.next = list[(static_cast<std::size_t>(k) + list.size() - 1) % list.size()];
  }

  // 5. Faces: the cycles of next, with their signed areas.
  for (std::size_t h0 = 0; h0 < out.half_edges.size(); ++h0) {
    if (out.half_edges[h0].face >= 0) continue;
    Arrangement::Face face{{}, 0.0, 0};
    const int id = static_cast<int>(out.faces.size());
    for (int h = static_cast<int>(h0); out.half_edges[at(h)].face < 0;
         h = out.half_edges[at(h)].next) {
      out.half_edges[at(h)].face = id;
      face.half_edges.push_back(h);
      face.area += swept_area(out.half_edges[at(h)].curve, 1e-6 * size) / 2;
    }
    out.faces.push_back(std::move(face));
  }

  // 6. Winding numbers at a point inside each bounded face: off the middle of its longest
  //    edge, halfway to the nearest curve on its left.
  for (Arrangement::Face& face : out.faces) {
    if (!(face.area > 1e-12 * size * size)) continue;
    int longest = face.half_edges.front();
    for (const int h : face.half_edges) {
      if (length(end_of(out.half_edges[at(h)].curve) - start_of(out.half_edges[at(h)].curve)) >
          length(end_of(out.half_edges[at(longest)].curve) -
                 start_of(out.half_edges[at(longest)].curve))) {
        longest = h;
      }
    }
    const NurbsCurve2& c = out.half_edges[at(longest)].curve;
    const double um = (c.domain_start() + c.domain_end()) / 2;
    std::array<Vec2, 2> d{};
    c.derivatives(um, 1, d);
    const Vec2 n = Vec2{-d[1].y, d[1].x} / length(d[1]);
    double nearest = INFINITY;
    for (std::size_t h = 0; h < out.half_edges.size(); h += 2) {
      for (const auto& hit : intersect_line(out.half_edges[h].curve, d[0], n).hits) {
        const double t = dot(hit.point - d[0], n);
        if (t > tol) nearest = std::min(nearest, t);
      }
    }
    if (!std::isfinite(nearest)) return std::unexpected("could not place a point inside a face");
    const auto w = winding_at(input, d[0] + n * (nearest / 2), tol);
    if (!w) return std::unexpected("winding number is ambiguous in every direction tried");
    face.winding = *w;
  }
  return out;
}

}  // namespace nf::math
