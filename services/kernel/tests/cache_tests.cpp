// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <random>
#include <vector>

#include "geometry_cache.hpp"

using nf::ir::Hash;
using nf::ir::sha256;
using nf::kernel_service::GeometryCache;

namespace {
using Cache = GeometryCache<std::string>;
auto output(std::string text) { return std::make_shared<const std::string>(std::move(text)); }
}  // namespace

TEST_CASE("geometry cache: lookups refresh least recently used order", "[cache]") {
  Cache cache({2, 100});
  const auto a = sha256("a"), b = sha256("b"), c = sha256("c");
  CHECK_FALSE(cache.find(a));
  REQUIRE(cache.insert(a, output("a"), 10));
  REQUIRE(cache.insert(b, output("b"), 20));
  CHECK(*cache.find(a) == "a");
  REQUIRE(cache.insert(c, output("c"), 30));
  CHECK_FALSE(cache.find(b));
  CHECK(*cache.find(a) == "a");
  CHECK(*cache.find(c) == "c");
  CHECK(cache.stats().entries == 2);
  CHECK(cache.stats().estimated_bytes == 40);
  CHECK(cache.stats().hits == 3);
  CHECK(cache.stats().misses == 2);
  CHECK(cache.stats().evictions == 1);
}

TEST_CASE("geometry cache: byte budget can evict several results", "[cache]") {
  Cache cache({10, 100});
  const auto a = sha256("a"), b = sha256("b"), c = sha256("c");
  REQUIRE(cache.insert(a, output("a"), 40));
  REQUIRE(cache.insert(b, output("b"), 40));
  REQUIRE(cache.insert(c, output("c"), 90));
  CHECK_FALSE(cache.find(a));
  CHECK_FALSE(cache.find(b));
  CHECK(cache.stats().entries == 1);
  CHECK(cache.stats().estimated_bytes == 90);
  CHECK(cache.stats().evictions == 2);
  CHECK_FALSE(cache.insert(a, output("oversized"), 101));
  CHECK_FALSE(cache.insert(c, {}, 1));
  CHECK(*cache.find(c) == "c");
}

TEST_CASE("geometry cache: replacements and eviction preserve live readers", "[cache]") {
  Cache cache({1, 100});
  const auto key = sha256("result");
  auto first = output("old immutable snapshot");
  const std::weak_ptr<const std::string> weak = first;
  REQUIRE(cache.insert(key, first, 10));
  auto reader = cache.find(key);
  first.reset();
  REQUIRE(cache.insert(key, output("replacement"), 25));
  CHECK(*cache.find(key) == "replacement");
  CHECK(*reader == "old immutable snapshot");
  CHECK_FALSE(weak.expired());
  CHECK(cache.stats().entries == 1);
  CHECK(cache.stats().estimated_bytes == 25);
  CHECK(cache.stats().evictions == 0);
  reader.reset();
  CHECK(weak.expired());
  auto retained = cache.find(key);
  cache.clear();
  CHECK(cache.stats().entries == 0);
  CHECK(cache.stats().estimated_bytes == 0);
  CHECK(*retained == "replacement");
}

TEST_CASE("geometry cache: limits shrink immediately and zero entries disables caching",
          "[cache]") {
  Cache cache({3, 100});
  const auto a = sha256("a"), b = sha256("b"), c = sha256("c");
  REQUIRE(cache.insert(a, output("a"), 20));
  REQUIRE(cache.insert(b, output("b"), 30));
  REQUIRE(cache.insert(c, output("c"), 40));
  cache.set_limits({2, 60});
  CHECK(cache.stats().entries == 1);
  CHECK(*cache.find(c) == "c");
  cache.set_limits({0, 100});
  CHECK(cache.stats().entries == 0);
  CHECK_FALSE(cache.insert(a, output("a"), 0));
  cache.set_limits({2, 0});
  CHECK_FALSE(cache.insert(a, output("a"), 1));
  REQUIRE(cache.insert(a, output("empty"), 0));
  CHECK(cache.stats().estimated_bytes == 0);
}

TEST_CASE("geometry cache: byte accounting cannot overflow", "[cache]") {
  const auto maximum = std::numeric_limits<std::size_t>::max();
  Cache cache({3, maximum});
  const auto a = sha256("a"), b = sha256("b");
  REQUIRE(cache.insert(a, output("a"), maximum - 1));
  REQUIRE(cache.insert(b, output("b"), 2));
  CHECK_FALSE(cache.find(a));
  CHECK(cache.stats().estimated_bytes == 2);
  REQUIRE(cache.insert(b, output("larger b"), maximum));
  CHECK(cache.stats().estimated_bytes == maximum);
  cache.set_limits({3, maximum - 1});
  CHECK(cache.stats().entries == 0);
  CHECK(cache.stats().estimated_bytes == 0);
}

TEST_CASE("geometry cache: randomized access agrees with a bounded reference model",
          "[cache][property]") {
  struct Entry {
    Hash key;
    std::size_t bytes;
  };
  Cache cache({5, 100});
  std::vector<Entry> model;
  std::size_t total = 0;
  std::mt19937 random(20261017);
  for (int i = 0; i < 2000; ++i) {
    const auto key = sha256(std::to_string(random() % 10));
    const auto found =
        std::find_if(model.begin(), model.end(), [&](const auto& e) { return e.key == key; });
    if (random() % 2 == 0) {
      CHECK(bool(cache.find(key)) == (found != model.end()));
      if (found != model.end()) {
        const auto entry = *found;
        model.erase(found);
        model.push_back(entry);
      }
    } else {
      const std::size_t bytes = random() % 110;
      CHECK(cache.insert(key, output(key.hex()), bytes) == (bytes <= 100));
      if (bytes <= 100) {
        if (found != model.end()) {
          total -= found->bytes;
          model.erase(found);
        }
        while (model.size() >= 5 || total > 100 - bytes) {
          total -= model.front().bytes;
          model.erase(model.begin());
        }
        model.push_back({key, bytes});
        total += bytes;
      }
    }
    CHECK(cache.stats().entries == model.size());
    CHECK(cache.stats().estimated_bytes == total);
  }
}
