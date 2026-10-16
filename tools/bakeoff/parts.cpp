// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "parts.hpp"

#include <numbers>

namespace nf::bakeoff {

namespace {

constexpr double kPi = std::numbers::pi;
// Selection regions are padded so entities on the boundary are inside.
constexpr double kPad = 0.01;

Region around(Vec3 a, Vec3 b) {
  return {{a.x - kPad, a.y - kPad, a.z - kPad}, {b.x + kPad, b.y + kPad, b.z + kPad}};
}

}  // namespace

std::vector<Part> reference_parts() {
  std::vector<Part> parts;

  // 10 × 20 × 30 block.
  parts.push_back({"block",
                   "primitive",
                   [](Kernel& k) { return k.box({0, 0, 0}, {10, 20, 30}); },
                   {.volume = 6000.0, .area = 2200.0}});

  // Sphere, r = 5.
  parts.push_back({"sphere",
                   "primitive",
                   [](Kernel& k) { return k.sphere({0, 0, 0}, 5.0); },
                   {.volume = 4.0 / 3.0 * kPi * 125.0, .area = 4.0 * kPi * 25.0}});

  // 12 × 12 × 1.5 plate with one r = 0.75 through-hole (the SMLIB-001 case).
  // V = 216 − π r² t;  A = 2(144 − π r²) + 4·12·t + 2π r t = 360 + 1.125π.
  parts.push_back({"plate_with_hole",
                   "boolean",
                   [](Kernel& k) {
                     const Body plate = k.box({0, 0, 0}, {12, 12, 1.5});
                     const Body tool = k.cylinder({1.5, 1.5, -1.0}, 0.75, 3.5);
                     return k.boolean(BooleanOp::kDifference, plate, tool);
                   },
                   {.volume = 216.0 - 0.84375 * kPi, .area = 360.0 + 1.125 * kPi}});

  // Two 10 mm cubes sharing the face x = 10: a coincident-face union.
  parts.push_back({"coincident_union",
                   "boolean",
                   [](Kernel& k) {
                     const Body a = k.box({0, 0, 0}, {10, 10, 10});
                     const Body b = k.box({10, 0, 0}, {10, 10, 10});
                     return k.boolean(BooleanOp::kUnion, a, b);
                   },
                   {.volume = 2000.0, .area = 1000.0}});

  // Block with its 30 mm edge on the Z axis filleted at r = 2.
  // V = 6000 − (1 − π/4) r² L = 5880 + 30π
  // A = 2200 − 2 r L + (π/2) r L − 2 (1 − π/4) r² = 2072 + 32π
  parts.push_back({"filleted_edge",
                   "fillet",
                   [](Kernel& k) {
                     const Body block = k.box({0, 0, 0}, {10, 20, 30});
                     return k.fillet(block, around({0, 0, 0}, {0, 0, 30}), 2.0,
                                     FilletKind::kCircular);
                   },
                   {.volume = 5880.0 + 30.0 * kPi, .area = 2072.0 + 32.0 * kPi}});

  // 20 mm cube shelled 2 mm inward with the top face open.
  // V = 20³ − 16·16·18 = 3392
  // A = outside 2000 + rim 144 + cavity floor 256 + cavity walls 1152 = 3552
  parts.push_back({"open_box_shell",
                   "shell",
                   [](Kernel& k) {
                     const Body cube = k.box({0, 0, 0}, {20, 20, 20});
                     return k.shell(cube, around({0, 0, 20}, {20, 20, 20}), 2.0);
                   },
                   {.volume = 3392.0, .area = 3552.0}});

  // Steinmetz solid: two r = 5 cylinders crossing at right angles (axes X and Y).
  // V = 16 r³ / 3;  A = 16 r²
  parts.push_back({"steinmetz",
                   "boolean",
                   [](Kernel& k) {
                     const Body z = k.cylinder({0, 0, -15}, 5.0, 30.0);
                     const Body y = k.rotate(z, {0, 0, 0}, {1, 0, 0}, 90.0);
                     const Body x = k.rotate(z, {0, 0, 0}, {0, 1, 0}, 90.0);
                     return k.boolean(BooleanOp::kIntersection, x, y);
                   },
                   {.volume = 16.0 / 3.0 * 125.0, .area = 16.0 * 25.0}});

  // Hemisphere: sphere r = 5 intersected with the half-space box z ≥ 0.
  // V = (2/3) π r³;  A = 2π r² + π r² = 75π
  parts.push_back({"hemisphere",
                   "boolean",
                   [](Kernel& k) {
                     const Body ball = k.sphere({0, 0, 0}, 5.0);
                     const Body half = k.box({-10, -10, 0}, {20, 20, 10});
                     return k.boolean(BooleanOp::kIntersection, ball, half);
                   },
                   {.volume = 2.0 / 3.0 * kPi * 125.0, .area = 75.0 * kPi}});

  // 20 × 20 × 10 block with a 10 × 10 × 5 pocket whose top is flush with the block's top
  // face (coincident faces). V = 4000 − 500;  A = 1600 − 100 + 100 + 200 = 1800
  parts.push_back({"flush_pocket",
                   "boolean",
                   [](Kernel& k) {
                     const Body block = k.box({0, 0, 0}, {20, 20, 10});
                     const Body tool = k.box({5, 5, 5}, {10, 10, 5});
                     return k.boolean(BooleanOp::kDifference, block, tool);
                   },
                   {.volume = 3500.0, .area = 1800.0}});

  // 30 × 10 × 10 bar with a 10 mm wide, 5 mm deep slot across its full width; the tool's
  // side faces are coplanar with the bar's front and back. V = 3000 − 500;
  // A = 1400 − 100 (top) + 100 (floor) + 100 (walls) − 100 (front/back) = 1400
  parts.push_back({"through_slot",
                   "boolean",
                   [](Kernel& k) {
                     const Body bar = k.box({0, 0, 0}, {30, 10, 10});
                     const Body tool = k.box({10, 0, 5}, {10, 10, 5});
                     return k.boolean(BooleanOp::kDifference, bar, tool);
                   },
                   {.volume = 2500.0, .area = 1400.0}});

  // 30 × 30 × 10 plate, r = 3 through-hole with an r = 5, 4 mm deep counterbore.
  // V = 9000 − 9π·10 − 16π·4 = 9000 − 154π
  // A = 3000 − 25π − 9π + 2π·3·6 + 2π·5·4 + π(25 − 9) = 3000 + 58π
  parts.push_back({"counterbore",
                   "boolean",
                   [](Kernel& k) {
                     const Body plate = k.box({0, 0, 0}, {30, 30, 10});
                     const Body hole = k.cylinder({15, 15, -1}, 3.0, 12.0);
                     const Body bore = k.cylinder({15, 15, 6}, 5.0, 5.0);
                     return k.boolean(BooleanOp::kDifference,
                                      k.boolean(BooleanOp::kDifference, plate, hole), bore);
                   },
                   {.volume = 9000.0 - 154.0 * kPi, .area = 3000.0 + 58.0 * kPi}});

  // 12 × 12 × 1.5 plate with a 4 × 4 grid of r = 0.75 holes at 3 mm pitch: 16 chained cuts.
  // V = 216 − 16π r² t = 216 − 13.5π;  A = 2(144 − 9π) + 72 + 16·2π r t = 360 + 18π
  parts.push_back({"hole_grid",
                   "boolean",
                   [](Kernel& k) {
                     Body plate = k.box({0, 0, 0}, {12, 12, 1.5});
                     for (int row = 0; row < 4; ++row) {
                       for (int col = 0; col < 4; ++col) {
                         const Body tool =
                             k.cylinder({(col + 0.5) * 3.0, (row + 0.5) * 3.0, -1.0}, 0.75, 3.5);
                         plate = k.boolean(BooleanOp::kDifference, plate, tool);
                       }
                     }
                     return plate;
                   },
                   {.volume = 216.0 - 13.5 * kPi, .area = 360.0 + 18.0 * kPi}});

  // 20 × 30 × 40 block with all 12 edges filleted at r = 3 (spherical vertex blends): the
  // Minkowski sum of a 14 × 24 × 34 core with a ball of radius 3.
  // V = a'b'c' + 2r(a'b' + b'c' + c'a') + π r²(a' + b' + c') + (4/3)π r³ = 21192 + 684π
  // A = 2(a'b' + b'c' + c'a') + 2π r(a' + b' + c') + 4π r² = 3256 + 468π
  parts.push_back({"rounded_box",
                   "fillet",
                   [](Kernel& k) {
                     const Body block = k.box({0, 0, 0}, {20, 30, 40});
                     return k.fillet(block, around({0, 0, 0}, {20, 30, 40}), 3.0,
                                     FilletKind::kCircular);
                   },
                   {.volume = 21192.0 + 684.0 * kPi, .area = 3256.0 + 468.0 * kPi}});

  // L-bracket: 40 × 10 × 4 base and 4 × 10 × 30 upright (overlap 4 × 10 × 4), the inside
  // corner filleted at r = 3 (concave: adds material), and an r = 3 hole through the base.
  // V = 1600 + 1200 − 160 + (1 − π/4)·9·10 − 9π·4 = 2730 − 58.5π
  parts.push_back({"l_bracket",
                   "fillet",
                   [](Kernel& k) {
                     const Body base = k.box({0, 0, 0}, {40, 10, 4});
                     const Body upright = k.box({0, 0, 0}, {4, 10, 30});
                     const Body l = k.boolean(BooleanOp::kUnion, base, upright);
                     const Body rounded =
                         k.fillet(l, around({4, 0, 4}, {4, 10, 4}), 3.0, FilletKind::kCircular);
                     const Body hole = k.cylinder({25, 5, -1}, 3.0, 6.0);
                     return k.boolean(BooleanOp::kDifference, rounded, hole);
                   },
                   {.volume = 2730.0 - 58.5 * kPi}});

  return parts;
}

}  // namespace nf::bakeoff
