// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use crate::c_void;
#[repr(C)]
#[derive(Default)]
pub(crate) struct Info {
    pub bytes: *const u8,
    pub byte_count: u64,
}
unsafe extern "C" {
    pub(crate) fn nf_native_export_step(
        context: *mut c_void,
        sources: *const *const c_void,
        source_count: u64,
        out: *mut *mut c_void,
    ) -> u32;
    pub(crate) fn nf_native_step_query(step: *const c_void, out: *mut Info) -> u32;
    pub(crate) fn nf_native_step_release(step: *mut c_void);
}
