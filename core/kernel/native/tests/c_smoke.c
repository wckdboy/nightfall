// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#include "nf_native.h"
_Static_assert(sizeof(uint32_t) == 4, "status width");
_Static_assert(offsetof(nf_native_info, identity) == 8, "identity layout");
int main(void) {
  nf_native_context* context = NULL;
  uint32_t status = nf_native_context_create(NF_NATIVE_ABI_VERSION, 1e-6, 1e-9, &context);
  if (NF_EXPECT_OCCT) {
    if (status != NF_NATIVE_OK || !context) return 1;
    nf_native_info info = {0};
    if (nf_native_context_info(context, &info) != NF_NATIVE_OK || info.abi_version != 1 ||
        info.capabilities != 511 || !info.identity || !info.identity_bytes)
      return 2;
  } else if (status != NF_NATIVE_UNAVAILABLE || context)
    return 3;
  nf_native_context_release(context);
  nf_native_context_release(NULL);
  return 0;
}
