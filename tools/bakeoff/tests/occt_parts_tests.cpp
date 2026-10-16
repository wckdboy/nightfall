// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// Every reference part must pass on OCCT. This checks the harness and the analytic
// expectations against a mature kernel before they are used to judge SMLib.

#include <catch2/catch_test_macros.hpp>

#include "runner.hpp"

using namespace nf::bakeoff;

TEST_CASE("every reference part passes on OCCT", "[occt]") {
  const auto kernel = make_occt_kernel();
  for (const Part& part : reference_parts()) {
    const PartResult r = run_part(*kernel, part);
    INFO(part.name << ": error='" << r.error << "' valid=" << r.measures.valid
                   << " volume=" << r.measures.volume << " area=" << r.measures.area);
    CHECK(r.success());
  }
}

TEST_CASE("OCCT reports G2 blends as unsupported instead of substituting", "[occt]") {
  const auto kernel = make_occt_kernel();
  const Body block = kernel->box({0, 0, 0}, {10, 10, 10});
  CHECK_THROWS_AS(kernel->fillet(block, {{-1, -1, -1}, {0.1, 0.1, 11}}, 1.0, FilletKind::kG2Blend),
                  KernelError);
}
