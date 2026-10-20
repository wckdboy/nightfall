// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use crate::{Kernel, KernelError, Shape, Step};
impl Kernel {
    /// Encode exact AP242 millimetre geometry, retaining every supplied body, including
    /// coincident ones. Source snapshots stay immutable. Empty or foreign-context lists
    /// return `KernelError::Native(NativeError::InvalidArgument)` before native access;
    /// solver failures retain their native diagnostics and publish no partial byte owner.
    ///
    /// The cloneable, thread-affine result borrows immutable bytes that outlive all sources
    /// and this facade. Copy `step.bytes().to_vec()` to send data across threads. This
    /// operation opens no files; STEP carries geometry without editable history/selectors.
    pub fn export_step(&self, shapes: &[Shape]) -> Result<Step, KernelError> {
        Ok(self.native.export_step(shapes)?)
    }
}
