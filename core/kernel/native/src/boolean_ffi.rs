// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use crate::c_void;
unsafe extern "C" {
    pub fn nf_native_boolean(
        context: *mut c_void,
        target: *const c_void,
        tools: *const *const c_void,
        tool_count: u64,
        mode: u32,
        out: *mut *mut c_void,
    ) -> u32;
}
