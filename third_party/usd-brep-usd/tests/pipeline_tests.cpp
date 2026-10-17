// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// The RFC 0002 route: an OCCT .brep -> USD BrepArray -> SMLib, volume preserved.

#include <OcctFileConverter.h>
#include <SmApiQueries.h>
#include <SmApiUsd.h>
#include <SmBrep.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <memory>
#include <string>
#include <vector>

double write_filleted_box_brep(const std::string& path);  // occt_side.cpp

TEST_CASE("an OCCT .brep reaches SMLib through USD with its volume intact", "[usd]") {
  const double occt_volume = write_filleted_box_brep("pipeline.brep");
  REQUIRE(occt_volume > 0.0);

  std::string error;
  REQUIRE(occt::ConvertOcctFileToUsd("pipeline.brep", "pipeline.usdc", error));
  INFO(error);

  std::vector<SmBrep*> raw;
  const SmStatus status = SmApiUsdImportBreps("pipeline.usdc", raw, /*bHealerIsEnabled=*/FALSE);
  const std::vector<std::unique_ptr<SmBrep>> breps(raw.begin(), raw.end());  // freed on any exit
  REQUIRE(status == SM_SUCCESS);
  REQUIRE(breps.size() == 1);

  SmBoolean manifold = FALSE;
  REQUIRE(SmApiBrepIsManifoldSolid(breps[0].get(), manifold) == SM_SUCCESS);
  CHECK(manifold == TRUE);
  double volume = 0.0;
  REQUIRE(SmApiBrepComputeVolume(breps[0].get(), 1e-8, volume) == SM_SUCCESS);
  CHECK_THAT(volume, Catch::Matchers::WithinRel(occt_volume, 1e-6));
}
