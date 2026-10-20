// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "nf_native.h"
#if NF_NATIVE_OCCT
#include "nf/kernel/kernel.hpp"
struct nf_native_context {
  std::shared_ptr<nf::kernel::IKernel> kernel;
};
#else
struct nf_native_context {};
#endif
