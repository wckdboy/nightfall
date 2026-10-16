// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <set>
#include <string>

#include "runner.hpp"

using namespace nf::bakeoff;

namespace {

// Returns fixed measures; fails any operation whose name is listed in `fail_on`.
class FakeKernel final : public Kernel {
 public:
  Measures next;
  std::set<std::string> fail_on;

  std::string_view name() const override { return "fake"; }
  std::string version() const override { return "1.0 \"test\""; }
  HistorySupport history_support() const override { return HistorySupport::kPartial; }

  Body box(Vec3, Vec3) override { return op("box"); }
  Body cylinder(Vec3, double, double) override { return op("cylinder"); }
  Body sphere(Vec3, double) override { return op("sphere"); }
  Body boolean(BooleanOp, Body, Body) override { return op("boolean"); }
  Body fillet(Body, Region, double, FilletKind) override { return op("fillet"); }
  Body shell(Body, Region, double) override { return op("shell"); }
  Measures measure(Body) override { return next; }

 private:
  Body op(const std::string& what) {
    if (fail_on.contains(what)) throw KernelError(what + " failed");
    return Body{0};
  }
};

Part box_part(Expectation expect) {
  return {"p", "primitive", [](Kernel& k) { return k.box({}, {1, 1, 1}); }, expect};
}

}  // namespace

TEST_CASE("a part succeeds only when built, valid and within tolerance", "[runner]") {
  FakeKernel k;
  k.next = {.valid = true, .volume = 1000.0 * (1 + 0.5e-6), .area = 600.0};
  const PartResult ok = run_part(k, box_part({.volume = 1000.0, .area = 600.0}));
  CHECK(ok.built);
  CHECK(ok.volume_ok == true);
  CHECK(ok.area_ok == true);
  CHECK(ok.success());

  k.next.volume = 1000.0 * (1 + 2e-6);  // outside 1e-6 relative
  CHECK_FALSE(run_part(k, box_part({.volume = 1000.0})).success());

  k.next = {.valid = false, .volume = 1000.0};
  CHECK_FALSE(run_part(k, box_part({.volume = 1000.0})).success());
}

TEST_CASE("kernel errors are recorded, not thrown", "[runner]") {
  FakeKernel k;
  k.fail_on = {"box"};
  const PartResult r = run_part(k, box_part({}));
  CHECK_FALSE(r.built);
  CHECK(r.error == "box failed");
  CHECK_FALSE(r.success());
}

TEST_CASE("unset expectations are not checked", "[runner]") {
  FakeKernel k;
  k.next = {.valid = true, .volume = 123.0};
  const PartResult r = run_part(k, box_part({}));
  CHECK_FALSE(r.volume_ok.has_value());
  CHECK(r.success());
}

TEST_CASE("JSON report escapes strings and writes null for missing values", "[runner]") {
  RunReport report{.kernel = "fake", .version = "1.0 \"q\"\n", .history = HistorySupport::kPartial};
  PartResult p{.name = "a\\b", .category = "c", .error = "tab\there"};
  p.measures.volume = std::numeric_limits<double>::quiet_NaN();
  report.parts.push_back(p);
  const std::string json = to_json(report);
  CHECK(json.find(R"("version":"1.0 \"q\"\n")") != std::string::npos);
  CHECK(json.find(R"("name":"a\\b")") != std::string::npos);
  CHECK(json.find(R"("error":"tab\there")") != std::string::npos);
  CHECK(json.find(R"("volume":null)") != std::string::npos);
  CHECK(json.find(R"("volume_ok":null)") != std::string::npos);
  CHECK(json.find(R"("history_support_declared":"partial")") != std::string::npos);
}

TEST_CASE("reference parts have unique names and analytic expectations", "[parts]") {
  std::set<std::string> names;
  for (const Part& part : reference_parts()) {
    CHECK(names.insert(part.name).second);
    CHECK(part.expect.volume.has_value());
  }
  CHECK(names.size() >= 6);
}
