// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "context.hpp"
#if NF_NATIVE_OCCT
struct nf_native_result {
  std::shared_ptr<nf::kernel::IKernel> backend;
  nf::kernel::OperationResult operation;
};
#else
struct nf_native_result {};
#endif
