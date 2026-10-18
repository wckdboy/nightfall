// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "nf/math/arrangement.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <set>

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

// Degree-1 Bézier spans are straight even with unequal rational weights. Recognize a
// common interval geometrically, using local scale and coordinate rounding, not the
// drawing-wide vertex-weld tolerance (which could conflate nearby parallel boundaries).
std::optional<std::array<Vec2, 2>> shared_line(const NurbsCurve2& a, const NurbsCurve2& b) {
  if (a.degree() != 1 || b.degree() != 1) return std::nullopt;
  const Vec2 delta = end_of(a) - start_of(a);
  const double len = length(delta), other_len = length(end_of(b) - start_of(b));
  if (!(len > 0 && other_len > 0)) return std::nullopt;
  const Vec2 dir = delta / len;
  double magnitude = 0;
  for (const auto p : {start_of(a), end_of(a), start_of(b), end_of(b)})
    magnitude = std::max({magnitude, std::abs(p.x), std::abs(p.y)});
  const double near =
      1e-12 * std::max(len, other_len) + 8 * std::numeric_limits<double>::epsilon() * magnitude;
  const Vec2 first = start_of(b) - start_of(a), last = end_of(b) - start_of(a);
  if (std::abs(cross(dir, first)) > near || std::abs(cross(dir, last)) > near) return std::nullopt;
  const double s = dot(dir, first), t = dot(dir, last);
  const double lo = std::max(0.0, std::min(s, t)), hi = std::min(len, std::max(s, t));
  if (hi - lo <= near) return std::nullopt;
  return std::array{start_of(a) + dir * lo, start_of(a) + dir * hi};
}

// Invert a rational line's geometric fraction to its original knot parameter. Interpolating
// knots directly by distance is only correct when both weights are equal.
double line_parameter(const NurbsCurve2& c, Vec2 p) {
  const Vec2 delta = end_of(c) - start_of(c);
  const double len = length(delta);
  const double s = std::clamp(dot(p - start_of(c), delta / len) / len, 0.0, 1.0);
  const double numerator = s * c.weights().front();
  const double t = numerator / (numerator + (1 - s) * c.weights().back());
  return std::lerp(c.domain_start(), c.domain_end(), t);
}

double trace_tolerance(const NurbsCurve2& a, const NurbsCurve2& b) {
  double magnitude = 0;
  for (const auto* c : {&a, &b})
    for (const auto p : c->points())
      magnitude = std::max({magnitude, std::abs(p.x), std::abs(p.y)});
  return 1e-12 * std::max(extent(a), extent(b)) +
         8 * std::numeric_limits<double>::epsilon() * magnitude;
}

// B(q*t/(1-t+q*t)) has weights w[i]*q^i. Log space avoids overflow; common
// scaling is immaterial. Unrepresentable weight ranges are not certified.
std::optional<std::vector<double>> parameter_weights(const NurbsCurve2& c, bool reversed,
                                                     double log_ratio) {
  std::vector<double> result;
  const auto w = c.weights();
  for (std::size_t i = 0; i < w.size(); ++i)
    result.push_back(std::log(w[reversed ? w.size() - 1 - i : i]) +
                     static_cast<double>(i) * log_ratio);
  const double maximum = std::ranges::max(result);
  for (auto& value : result) {
    value = std::exp(value - maximum);
    if (!(value > 0 && std::isfinite(value))) return std::nullopt;
  }
  return result;
}
double endpoint_speed(const NurbsCurve2& c, bool end) {
  const auto p = c.points();
  const auto w = c.weights();
  const std::size_t i = end ? p.size() - 1 : 0, j = end ? i - 1 : 1;
  return std::log(c.degree()) + std::log(w[j]) - std::log(w[i]) + std::log(length(p[j] - p[i]));
}
double choose(int n, int k) {
  double result = 1;
  for (int i = 1; i <= k; ++i) result *= static_cast<double>(n - i + 1) / i;
  return result;
}

// Prove equality on the entire normalized span, including different degrees, by the
// Bernstein coefficients of N_a W_b - N_b W_a. Bound each residual by its positive
// denominator coefficient, so tiny weights cannot hide a real geometric difference.
bool same_trace(const NurbsCurve2& a, const NurbsCurve2& b, bool reversed = false) {
  const double near = trace_tolerance(a, b);
  if (!std::isfinite(near)) return false;
  const auto point_b = [&](int j) { return b.points()[at(reversed ? b.degree() - j : j)]; };
  if (length(start_of(a) - point_b(0)) > near || length(end_of(a) - point_b(b.degree())) > near)
    return false;
  const auto matches = [&](double log_ratio) {
    if (!std::isfinite(log_ratio)) return false;
    const auto wb = parameter_weights(b, reversed, log_ratio);
    if (!wb) return false;
    for (int k = 0; k <= a.degree() + b.degree(); ++k) {
      Vec2 residual;
      double denominator = 0;
      for (int i = std::max(0, k - b.degree()); i <= std::min(a.degree(), k); ++i) {
        const int j = k - i;
        const double weight =
            choose(a.degree(), i) * choose(b.degree(), j) * a.weights()[at(i)] * (*wb)[at(j)];
        residual = residual + (a.points()[at(i)] - point_b(j)) * weight;
        denominator += weight;
      }
      if (!(denominator > 0 && length(residual) <= near * denominator)) return false;
    }
    return true;
  };
  // Endpoint speed ratios propose q independently of degree elevation. The coefficient
  // check still decides; neither tangent agreement nor endpoint agreement is sufficient.
  return matches(0) || matches(endpoint_speed(a, false) - endpoint_speed(b, reversed)) ||
         matches(endpoint_speed(b, !reversed) - endpoint_speed(a, true));
}

NurbsCurve2 interval(NurbsCurve2 c, double low, double high) {
  if (low > c.domain_start()) c = split(c, low).value().second;
  if (high < c.domain_end()) c = split(c, high).value().first;
  return c;
}

// Endpoints propose a common interval; only a whole-curve coefficient proof accepts it.
// Each pair stores the original parameters, with b's direction allowed to be reversed.
std::optional<std::array<double, 4>> shared_curve(const NurbsCurve2& a, const NurbsCurve2& b,
                                                  bool partial = false) {
  if (same_trace(a, b))
    return std::array{a.domain_start(), a.domain_end(), b.domain_start(), b.domain_end()};
  if (same_trace(a, b, true))
    return std::array{a.domain_start(), a.domain_end(), b.domain_end(), b.domain_start()};
  if (!partial) return std::nullopt;
  const double near = trace_tolerance(a, b);
  std::vector<std::pair<double, double>> ends;
  for (const double u : {a.domain_start(), a.domain_end()}) {
    const auto hit = closest_point(b, a.point(u));
    if (hit.distance <= near) ends.emplace_back(u, hit.u);
  }
  for (const double u : {b.domain_start(), b.domain_end()}) {
    const auto hit = closest_point(a, b.point(u));
    if (hit.distance <= near) ends.emplace_back(hit.u, u);
  }
  if (ends.size() < 2) return std::nullopt;
  std::ranges::sort(ends);
  const auto [a0, b0] = ends.front();
  const auto [a1, b1] = ends.back();
  if (!(a1 > a0) || b0 == b1 || length(a.point(a1) - a.point(a0)) <= near) return std::nullopt;
  if (!same_trace(interval(a, a0, a1), interval(b, std::min(b0, b1), std::max(b0, b1)), b1 < b0))
    return std::nullopt;
  return std::array{a0, a1, b0, b1};
}

// Positive-weight Bezier curves stay in their control hull. If that hull lies on
// one side of a line, only an endpoint can touch it: use that exact endpoint rather
// than an ill-conditioned Newton root beside a newly introduced span boundary.
std::optional<CurveCurveIntersection> line_contacts(const NurbsCurve2& line,
                                                    const NurbsCurve2& curve) {
  if (line.degree() != 1 || curve.degree() == 1) return std::nullopt;
  const auto delta = end_of(line) - start_of(line);
  const double len = length(delta), near = trace_tolerance(line, curve);
  if (!(len > 0)) return std::nullopt;
  const auto dir = delta / len;
  const auto distance = [&](Vec2 p) { return cross(dir, p - start_of(line)); };
  double low = INFINITY, high = -INFINITY;
  for (const auto p : curve.points()) {
    low = std::min(low, distance(p));
    high = std::max(high, distance(p));
  }
  if (!((low >= -near && high > near) || (high <= near && low < -near))) return std::nullopt;
  CurveCurveIntersection result;
  for (const double u : {curve.domain_start(), curve.domain_end()}) {
    const auto p = curve.point(u);
    const double along = dot(p - start_of(line), dir);
    if (std::abs(distance(p)) <= near && along >= -near && along <= len + near)
      result.hits.push_back({line_parameter(line, p), u, p});
  }
  return result;
}

// Half-edges leaving a vertex along (nearly) the same tangent, as where curves touch, are
// ordered by where they actually go: the direction of the chord to a point a short, common
// arc length along each, measured from the run's first tangent (more clockwise first, as
// angles increase counter-clockwise). Tangencies are solved only to ~1e-8, so tangents that
// should coincide differ by about that much; ties are taken within 1e-6 rad, where the
// chords' curvature term dominates. Runs may wrap around ±π.
void order_tangent_ties(std::vector<int>& list, const std::vector<Arrangement::HalfEdge>& he,
                        const std::vector<double>& angle) {
  const std::size_t n = list.size();
  if (n < 2) return;
  const auto same = [&](int a, int b) {
    const double d = std::abs(angle[at(a)] - angle[at(b)]);
    return std::min(d, 2 * std::numbers::pi - d) <= 1e-6;
  };
  // Start at a gap between different angles, so no run wraps around the list's end.
  std::size_t start = 0;
  while (start < n && same(list[(start + n - 1) % n], list[start])) ++start;
  if (start == n) start = 0;
  std::rotate(list.begin(), list.begin() + static_cast<std::ptrdiff_t>(start), list.end());
  for (std::size_t i = 0; i < n;) {
    std::size_t j = i + 1;
    while (j < n && same(list[i], list[j])) ++j;
    if (j - i > 1) {
      double arc = INFINITY;  // a common arc length, short for every curve in the run
      for (std::size_t k = i; k < j; ++k) arc = std::min(arc, 1e-4 * extent(he[at(list[k])].curve));
      const Vec2 ref = leaving(he[at(list[i])].curve);
      const auto heading = [&](int h) {
        const NurbsCurve2& c = he[at(h)].curve;
        std::array<Vec2, 2> d{};
        c.derivatives(c.domain_start(), 1, d);
        const double du =
            length(d[1]) > 0 ? arc / length(d[1]) : 1e-6 * (c.domain_end() - c.domain_start());
        const Vec2 chord = c.point(std::min(c.domain_start() + du, c.domain_end())) - d[0];
        return std::atan2(cross(ref, chord), dot(ref, chord));
      };
      std::sort(list.begin() + static_cast<std::ptrdiff_t>(i),
                list.begin() + static_cast<std::ptrdiff_t>(j),
                [&](int a, int b) { return heading(a) < heading(b); });
    }
    i = j;
  }
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
std::optional<int> winding_at(std::span<const NurbsCurve2> input, Vec2 p, double tol) {
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
  std::vector<NurbsCurve2> originals;
  std::vector<std::size_t> parents;
  Vec2 lo{INFINITY, INFINITY}, hi{-INFINITY, -INFINITY};
  for (const auto& path : paths) {
    if (path.empty()) continue;
    for (const NurbsCurve2& c : path) {
      const auto parent = originals.size();
      if (c.degree() > 1) originals.push_back(c);
      for (const auto& span : decompose(c)) {
        parents.push_back(c.degree() > 1 ? parent : originals.size());
        if (c.degree() == 1) originals.push_back(span);
        input.push_back(span);
      }
      for (const Vec2 q : c.points()) {
        lo = {std::min(lo.x, q.x), std::min(lo.y, q.y)};
        hi = {std::max(hi.x, q.x), std::max(hi.y, q.y)};
      }
    }
    if (end_of(path.back()) != start_of(path.front())) {
      parents.push_back(originals.size());
      input.push_back(make_line(end_of(path.back()), start_of(path.front())));
      originals.push_back(input.back());
    }
  }
  Arrangement out;
  if (input.empty()) return out;
  const double size = std::max(length(hi - lo), 1e-300);
  const double tol = 1e-9 * size;

  // 2. Split parameters where curves meet (crossings, touches, T-junctions).
  std::vector<std::vector<double>> cuts(input.size());
  std::set<std::pair<std::size_t, std::size_t>> shared_curves;
  std::map<std::pair<std::size_t, std::size_t>, CurveCurveIntersection> intersections;
  for (std::size_t i = 0; i < input.size(); ++i) {
    if (i == 0 || parents[i] != parents[i - 1]) intersections.clear();
    for (std::size_t j = i + 1; j < input.size(); ++j) {
      if (parents[i] == parents[j]) continue;  // own self-intersections remain unsupported
      if (const auto shared = shared_line(input[i], input[j])) {
        for (const auto p : *shared) {
          cuts[i].push_back(line_parameter(input[i], p));
          cuts[j].push_back(line_parameter(input[j], p));
        }
        continue;
      }
      const auto record = [&](const std::array<double, 4>& shared) {
        shared_curves.emplace(i, j);
        cuts[i].insert(cuts[i].end(), {shared[0], shared[1]});
        cuts[j].insert(cuts[j].end(), {shared[2], shared[3]});
      };
      if (const auto shared = shared_curve(input[i], input[j])) {
        record(*shared);
        continue;
      }
      const auto r = [&] {
        if (const auto contacts = line_contacts(input[i], input[j])) return *contacts;
        if (auto contacts = line_contacts(input[j], input[i])) {
          for (auto& h : contacts->hits) std::swap(h.ua, h.ub);
          return *contacts;
        }
        // Keep ordinary roots on the original curves: splitting a tangent at a knot
        // beforehand can turn one endpoint contact into several imprecise nearby hits.
        auto [cached, inserted] = intersections.try_emplace({parents[i], parents[j]});
        if (inserted) cached->second = intersect(originals[parents[i]], originals[parents[j]]);
        const auto& original = cached->second;
        return original.overlap ? intersect(input[i], input[j]) : original;
      }();
      if (r.overlap) {
        if (const auto shared = shared_curve(input[i], input[j], true)) {
          record(*shared);
          continue;
        }
        return std::unexpected(
            "curves share a stretch whose common parameterization could not be certified (#74)");
      }
      for (const auto& h : r.hits) {
        if (h.ua < input[i].domain_start() || h.ua > input[i].domain_end() ||
            h.ub < input[j].domain_start() || h.ub > input[j].domain_end())
          continue;
        const auto interior = [&](const NurbsCurve2& c, double u) {
          return length(c.point(u) - start_of(c)) > tol && length(c.point(u) - end_of(c)) > tol;
        };
        if (interior(input[i], h.ua)) cuts[i].push_back(h.ua);
        if (interior(input[j], h.ub)) cuts[j].push_back(h.ub);
      }
    }
  }

  // 3. Edges: the pieces between cuts, endpoints snapped to shared vertices.
  std::vector<std::size_t> edge_sources;
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
      bool merged = false;
      for (std::size_t h = 0; h < out.half_edges.size(); h += 2) {
        auto& edge = out.half_edges[h];
        const bool same = edge.origin == a && edge.target == b;
        const bool opposite = edge.origin == b && edge.target == a;
        if (!(same || opposite)) continue;
        const bool line = shared_line(piece, edge.curve).has_value();
        // A tiny piece near a tangency can agree to rounding even when its parent
        // curves are distinct. Only certified parent overlaps may merge curved pieces.
        if (!line && !shared_curves.contains({edge_sources[h / 2], i})) continue;
        const int direction = same && (line || same_trace(piece, edge.curve))             ? 1
                              : opposite && (line || same_trace(piece, edge.curve, true)) ? -1
                                                                                          : 0;
        if (!direction) continue;
        edge.direction += direction;
        out.half_edges[h + 1].direction = -edge.direction;
        merged = true;
        break;
      }
      if (merged) continue;  // zero-winding seams still delimit arrangement faces
      const int f = static_cast<int>(out.half_edges.size());
      edge_sources.push_back(i);
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
  for (auto& list : around) {
    std::ranges::sort(list, {}, [&](int h) { return angle[at(h)]; });
    order_tangent_ties(list, out.half_edges, angle);
  }
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

  // 6. Winding numbers at a point on the left of each cycle: off its longest edge, halfway
  //    to the nearest curve there. Every cycle gets one, however small: the outer cycle of a
  //    part nested inside another face borders that face, so it shares its winding number;
  //    with no curve on its left, a cycle borders the unbounded face (winding 0).
  for (Arrangement::Face& face : out.faces) {
    int longest = face.half_edges.front();
    for (const int h : face.half_edges) {
      if (extent(out.half_edges[at(h)].curve) > extent(out.half_edges[at(longest)].curve))
        longest = h;
    }
    const NurbsCurve2& c = out.half_edges[at(longest)].curve;
    // A parameter where the curve is not stationary (a cubic can stop at its midpoint).
    std::array<Vec2, 2> d{};
    for (const double f : {0.5, 0.37, 0.63, 0.21, 0.79}) {
      c.derivatives(c.domain_start() + f * (c.domain_end() - c.domain_start()), 1, d);
      if (length(d[1]) > 0) break;
    }
    if (!(length(d[1]) > 0)) return std::unexpected("an edge has no direction at any sample");
    const Vec2 n = Vec2{-d[1].y, d[1].x} / length(d[1]);
    double nearest = INFINITY;
    for (std::size_t h = 0; h < out.half_edges.size(); h += 2) {
      for (const auto& hit : intersect_line(out.half_edges[h].curve, d[0], n).hits) {
        const double t = dot(hit.point - d[0], n);
        if (t > tol) nearest = std::min(nearest, t);
      }
    }
    if (!std::isfinite(nearest)) {
      if (face.area > 0) return std::unexpected("could not place a point inside a face");
      continue;  // the unbounded face
    }
    const auto w = winding_at(input, d[0] + n * (nearest / 2), tol);
    if (!w) return std::unexpected("winding number is ambiguous in every direction tried");
    face.winding = *w;
  }
  return out;
}

std::optional<int> winding_number(std::span<const NurbsCurve2> curves, Vec2 p) {
  if (curves.empty()) return 0;
  Vec2 lo = curves.front().points().front(), hi = lo;
  for (const NurbsCurve2& c : curves) {
    for (const Vec2 q : c.points()) {
      lo = {std::min(lo.x, q.x), std::min(lo.y, q.y)};
      hi = {std::max(hi.x, q.x), std::max(hi.y, q.y)};
    }
  }
  const double tol = 1e-9 * std::max(length(hi - lo), 1e-300);
  // On a curve the winding number is undefined; rays from there would disagree.
  for (const NurbsCurve2& c : curves) {
    if (closest_point(c, p).distance <= tol) return std::nullopt;
  }
  return winding_at(curves, p, tol);
}

std::expected<std::vector<Region>, std::string> regions(
    std::span<const std::vector<NurbsCurve2>> paths, FillRule rule) {
  auto arranged = arrange(paths);
  if (!arranged) return std::unexpected(arranged.error());
  const Arrangement& a = *arranged;
  const auto filled = [&](int face) {
    const int w = a.faces[at(face)].winding;
    return rule == FillRule::kNonZero ? w != 0 : w % 2 != 0;
  };
  const auto boundary = [&](int h) {
    const auto& he = a.half_edges[at(h)];
    return filled(he.face) && !filled(a.half_edges[at(he.twin)].face);
  };

  // Boundary loops, filled side on the left. From a boundary half-edge, the loop continues
  // with the first boundary half-edge clockwise of its twin at the target: next(twin(c)) is
  // the outgoing half-edge just clockwise of c, so rotating that way finds it.
  std::vector<bool> used(a.half_edges.size(), false);
  std::vector<Loop> outers, holes;
  std::vector<double> outer_areas;
  for (std::size_t h0 = 0; h0 < a.half_edges.size(); ++h0) {
    if (used[h0] || !boundary(static_cast<int>(h0))) continue;
    Loop loop;
    double area2 = 0.0;
    double size = 0.0;
    for (int h = static_cast<int>(h0); !used[at(h)];) {
      used[at(h)] = true;
      const NurbsCurve2& c = a.half_edges[at(h)].curve;
      loop.curves.push_back(c);
      size = std::max(size, extent(c));
      int c_next = a.half_edges[at(h)].next;
      while (!boundary(c_next)) c_next = a.half_edges[at(a.half_edges[at(c_next)].twin)].next;
      h = c_next;
    }
    for (const NurbsCurve2& c : loop.curves) area2 += swept_area(c, 1e-6 * std::max(size, 1e-300));
    if (area2 > 0) {
      outers.push_back(std::move(loop));
      outer_areas.push_back(area2 / 2);
    } else {
      holes.push_back(std::move(loop));
    }
  }

  // Each hole belongs to the smallest outer loop around it.
  std::vector<Region> out;
  for (Loop& o : outers) out.push_back({std::move(o), {}});
  for (Loop& hole : holes) {
    const NurbsCurve2& c = hole.curves.front();
    const Vec2 probe = c.point((c.domain_start() + c.domain_end()) / 2);
    int best = -1;
    for (std::size_t i = 0; i < out.size(); ++i) {
      const auto w = winding_number(out[i].outer.curves, probe);
      if (w && *w != 0 && (best < 0 || outer_areas[i] < outer_areas[at(best)]))
        best = static_cast<int>(i);
    }
    if (best < 0) return std::unexpected("a hole lies outside every outer boundary");
    out[at(best)].holes.push_back(std::move(hole));
  }
  return out;
}

}  // namespace nf::math
