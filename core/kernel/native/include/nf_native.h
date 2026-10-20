// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#define NF_NATIVE_NOEXCEPT noexcept
#else
#define NF_NATIVE_NOEXCEPT
#endif
#define NF_NATIVE_ABI_VERSION UINT32_C(1)
enum {
  NF_NATIVE_OK,
  NF_NATIVE_INVALID,
  NF_NATIVE_UNAVAILABLE,
  NF_NATIVE_FAILURE,
  NF_NATIVE_ABI_MISMATCH
};
typedef struct nf_native_context nf_native_context;
typedef struct {
  uint32_t abi_version, capabilities;
  const uint8_t* identity;
  size_t identity_bytes;
} nf_native_info;
// Service-private ABI (RFC 0008). All calls are serialized, including create/release.
// Raw callers must supply aligned, live pointers from this adapter; forged/stale pointers
// cannot be checked. Keep each owner on its creating OS thread. No function throws.
// Linear/angular tolerances are mm/radians; OCCT precision floors remain binding.
// Failure clears *out; success transfers one owner, released only by context_release.
uint32_t nf_native_context_create(uint32_t abi, double linear, double angular,
                                  nf_native_context** out) NF_NATIVE_NOEXCEPT;
// Clears *out on failure. Identity is immutable UTF-8, NOT NUL-terminated by contract,
// borrowed until release. Capability bits 0..8: extrude, rigid transform, booleans,
// revolve, mirror, chamfer, fillet, loft, sweep. These describe the native solver.
uint32_t nf_native_context_info(const nf_native_context*, nf_native_info* out) NF_NATIVE_NOEXCEPT;
void nf_native_context_release(nf_native_context*) NF_NATIVE_NOEXCEPT;  // NULL-safe
#ifdef __cplusplus
}
#endif
#undef NF_NATIVE_NOEXCEPT
