// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <list>
#include <memory>
#include <unordered_map>

#include "nf/ir/hash.hpp"

namespace nf::kernel_service {
/// Service-owned LRU for immutable, successfully evaluated outputs (RFC 0005). Output owns
/// its kernel handles and naming history through RAII; no backend header reaches this file.
/// Callers serialize access and must never mutate an admitted snapshot, including its
/// transitively owned geometry. Keys include evaluator/backend versions and all dependencies.
template <class Output>
class GeometryCache {
 public:
  struct Limits {
    std::size_t entries, estimated_bytes;
  };
  struct Stats {
    std::size_t entries, estimated_bytes, hits, misses, evictions;
  };
  explicit GeometryCache(Limits limits) : limits_(limits) {}
  GeometryCache(const GeometryCache&) = delete;
  GeometryCache& operator=(const GeometryCache&) = delete;

  /// A hit refreshes recency and returns shared ownership. Eviction/clear releases only the
  /// cache's ownership: in-flight readers keep their snapshots alive. A miss returns null.
  [[nodiscard]] std::shared_ptr<const Output> find(const ir::Hash& key) {
    const auto found = index_.find(key);
    if (found == index_.end()) {
      ++misses_;
      return {};
    }
    ++hits_;
    order_.splice(order_.begin(), order_, found->second);
    return found->second->output;
  }
  /// Admit a complete success only. Cost includes geometry, provenance and retained data;
  /// it is a caller estimate, not a claim about process RSS. Entry count also caps zero-cost
  /// outputs. Null/oversized entries and disabled caches return false without changing state.
  /// Replacing a key refreshes recency and cost; it is not counted as a capacity eviction.
  [[nodiscard]] bool insert(ir::Hash key, std::shared_ptr<const Output> output,
                            std::size_t estimated_bytes) {
    if (!output || limits_.entries == 0 || estimated_bytes > limits_.estimated_bytes) return false;
    if (const auto existing = index_.find(key); existing != index_.end()) discard(existing->second);
    // Subtract before adding so even a SIZE_MAX budget cannot overflow accounting.
    while (order_.size() >= limits_.entries || bytes_ > limits_.estimated_bytes - estimated_bytes)
      evict();
    order_.push_front({key, std::move(output), estimated_bytes});
    try {
      index_.emplace(key, order_.begin());
    } catch (...) {
      order_.pop_front();
      throw;
    }
    bytes_ += estimated_bytes;
    return true;
  }
  /// Enforces new limits immediately, evicting least-recently-used entries first.
  void set_limits(Limits limits) {
    limits_ = limits;
    while (order_.size() > limits.entries || bytes_ > limits.estimated_bytes) evict();
  }
  /// Releases all cached ownership; cumulative counters are retained.
  void clear() {
    index_.clear();
    order_.clear();
    bytes_ = 0;
  }
  [[nodiscard]] Stats stats() const { return {order_.size(), bytes_, hits_, misses_, evictions_}; }

 private:
  struct Entry {
    ir::Hash key;
    std::shared_ptr<const Output> output;
    std::size_t bytes;
  };
  struct BucketHash {
    std::size_t operator()(const ir::Hash& key) const noexcept {
      std::size_t result = 0;
      for (auto byte : key.bytes) result = result * 131 + byte;
      return result;  // Table bucket only; equality compares the entire SHA-256 digest.
    }
  };
  using Order = std::list<Entry>;
  void discard(typename Order::iterator entry) {
    bytes_ -= entry->bytes;
    index_.erase(entry->key);
    order_.erase(entry);
  }
  void evict() {
    discard(std::prev(order_.end()));
    ++evictions_;
  }
  Limits limits_;
  Order order_;
  std::unordered_map<ir::Hash, typename Order::iterator, BucketHash> index_;
  std::size_t bytes_ = 0, hits_ = 0, misses_ = 0, evictions_ = 0;
};
}  // namespace nf::kernel_service
