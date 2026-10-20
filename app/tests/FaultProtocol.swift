// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
import Foundation

// Compiled only into test fixtures, never the production service or application.
@objc(NFTestFaultProtocol) protocol FaultProtocol: KernelServiceProtocol {
  func fault(_ mode: String, reply: @escaping KernelReply)
}
