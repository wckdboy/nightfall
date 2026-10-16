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

  return parts;
}

}  // namespace nf::bakeoff
