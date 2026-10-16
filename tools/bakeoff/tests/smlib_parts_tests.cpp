// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// The SMLib driver must run every reference part to a recorded result (pass or fail) and
// leave its inputs untouched. Whether SMLib passes is a bake-off result, not a test.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "runner.hpp"

using namespace nf::bakeoff;
using Catch::Matchers::WithinRel;

TEST_CASE("every reference part runs to a recorded result on SMLib", "[smlib]") {
  const auto kernel = make_smlib_kernel();
  for (const Part& part : reference_parts()) {
    const PartResult r = run_part(*kernel, part);
    INFO(part.name << ": built=" << r.built << " error='" << r.error
                   << "' valid=" << r.measures.valid << " volume=" << r.measures.volume
                   << " area=" << r.measures.area);
    CHECK((r.built || !r.error.empty()));
  }
}

TEST_CASE("SMLib primitives have exact volumes", "[smlib]") {
  const auto kernel = make_smlib_kernel();
  const Measures m = kernel->measure(kernel->box({0, 0, 0}, {10, 20, 30}));
  CHECK(m.valid);
  CHECK_THAT(m.volume, WithinRel(6000.0, kRelativeTolerance));
  CHECK(m.faces == 6);
  CHECK(m.edges == 12);
}

TEST_CASE("SMLib operations leave their inputs untouched", "[smlib]") {
  const auto kernel = make_smlib_kernel();
  const Body plate = kernel->box({0, 0, 0}, {12, 12, 1.5});
  const Body tool = kernel->box({2, 2, -1}, {3, 3, 3.5});
  const double before = kernel->measure(plate).volume;
  (void)kernel->boolean(BooleanOp::kDifference, plate, tool);
  CHECK_THAT(kernel->measure(plate).volume, WithinRel(before, 1e-12));
  CHECK_THAT(kernel->measure(tool).volume, WithinRel(31.5, kRelativeTolerance));
}

TEST_CASE("SMLib validity requires exactly one solid", "[smlib]") {
  const auto kernel = make_smlib_kernel();
  const Body a = kernel->box({0, 0, 0}, {10, 10, 10});
  const Body b = kernel->box({20, 0, 0}, {10, 10, 10});  // disjoint: union is two solids
  Body both;
  try {
    both = kernel->boolean(BooleanOp::kUnion, a, b);
  } catch (const KernelError&) {
    SUCCEED("SMLib rejects the disjoint union outright");
    return;
  }
  const Measures two = kernel->measure(both);
  INFO("faces=" << two.faces << " volume=" << two.volume);
  CHECK_FALSE(two.valid);
}
