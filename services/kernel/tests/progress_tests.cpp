// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "evaluator.hpp"
#include "part_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace nf;
using namespace nf::kernel_service;
TEST_CASE("evaluator reports only executing features in order before work including cache hits",
          "[progress]") {
  auto kernel = kernel::make_occt_kernel().value();
  Evaluator evaluator(*kernel, {96, 64 * 1024 * 1024});
  std::vector<std::size_t> observed;
  const auto observer = [&](std::size_t index) { observed.push_back(index); };
  auto d = test::part(5);
  d.features[2].suppressed = true;
  d.history_end = "move_3";
  const auto text = test::source(d);
  const auto cold = evaluator.evaluate(text, observer).value();
  REQUIRE(cold.ok());
  CHECK(observed == std::vector<std::size_t>{0, 1, 3});
  observed.clear();
  REQUIRE(evaluator.evaluate(text, observer).value().ok());
  CHECK(observed == std::vector<std::size_t>{0, 1, 3});
  observed.clear();
  CHECK_FALSE(evaluator.evaluate("{}", observer));
  CHECK(observed.empty());
  d.params[0].value = "0 mm";
  REQUIRE_FALSE(evaluator.evaluate(test::source(d), observer).value().ok());
  CHECK(observed == std::vector<std::size_t>{0, 1, 3});
}
