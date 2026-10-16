// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// SPIKE (RFC 0002): load one STEP file into OCCT and into SMLib (via .brep -> USD) and
// compare volumes. Usage: nf_usd_spike <in.step> <work-dir>   (PXR_PLUGINPATH_NAME must
// point at usd-brep's source/schema/omniSolid/resources)

#include <SmApiQueries.h>
#include <SmApiUsd.h>
#include <SmBrep.h>

#include <OcctFileConverter.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <cmath>
#include <vector>

bool step_to_brep(const std::string& step, const std::string& brep, double& volume);  // occt_side.cpp

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  const std::string step = argv[1], dir = argv[2];
  using clock = std::chrono::steady_clock;

  auto t0 = clock::now();
  const std::string brep = dir + "/model.brep", usd = dir + "/model.usdc";
  double occt_volume = 0.0;
  if (!step_to_brep(step, brep, occt_volume)) {
    std::printf("STEP read or .brep write failed\n");
    return 1;
  }
  auto t1 = clock::now();

  std::string error;
  if (!occt::ConvertOcctFileToUsd(brep, usd, error)) {
    std::printf(".brep -> USD failed: %s\n", error.c_str());
    return 1;
  }
  auto t2 = clock::now();

  std::vector<SmBrep*> breps;
  const SmStatus status = SmApiUsdImportBreps(usd.c_str(), breps, /*healer=*/FALSE);
  auto t3 = clock::now();
  if (status != SM_SUCCESS || breps.empty()) {
    std::printf("USD -> SMLib failed: status %d, %zu breps\n", static_cast<int>(status), breps.size());
    return 1;
  }
  double sm_volume = 0.0;
  for (SmBrep* b : breps) {
    double v = 0.0;
    SmApiBrepComputeVolume(b, 1e-8, v);
    sm_volume += v;
  }
  const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
  std::printf("breps=%zu  occt V=%.9g  smlib V=%.9g  rel diff=%.2e\n", breps.size(), occt_volume,
              sm_volume, std::abs(sm_volume - occt_volume) / std::abs(occt_volume));
  std::printf("timing ms: step+brep %.1f  brep->usd %.1f  usd->smlib %.1f\n", ms(t0, t1), ms(t1, t2), ms(t2, t3));
  for (SmBrep* b : breps) delete b;
  return 0;
}
