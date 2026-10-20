// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
use super::{
    c_void,
    extrusion::{Frame, Measures},
};
#[repr(C)]
pub(crate) struct Point {
    pub x: f64,
    pub y: f64,
}
#[repr(C)]
pub(crate) struct Curve {
    pub degree: u32,
    pub knots: *const f64,
    pub knot_count: usize,
    pub points: *const Point,
    pub point_count: usize,
    pub weights: *const f64,
    pub weight_count: usize,
}
#[repr(C)]
pub(crate) struct Loop {
    pub curves: *const Curve,
    pub count: usize,
}
#[repr(C)]
pub(crate) struct Region {
    pub outer: Loop,
    pub holes: *const Loop,
    pub hole_count: usize,
}
#[repr(C)]
pub(crate) struct Extrusion {
    pub frame: Frame,
    pub displacement: [f64; 3],
    pub regions: *const Region,
    pub region_count: usize,
}
#[repr(C)]
#[derive(Default)]
pub(crate) struct Info {
    pub measures: Measures,
    pub estimated_bytes: u64,
    pub generated_count: u64,
    pub history_count: u64,
}
#[repr(C)]
#[derive(Default)]
pub(crate) struct Generated {
    pub role: *const u8,
    pub role_bytes: usize,
    pub entity: *const c_void,
    pub region: u64,
    pub r#loop: u64,
    pub curve: u64,
    pub section: u64,
    pub has_from: u32,
    pub kind: u32,
}
unsafe extern "C" {
    pub fn nf_native_extrude(
        context: *mut c_void,
        input: *const Extrusion,
        out: *mut *mut c_void,
    ) -> u32;
    pub fn nf_native_result_query(result: *const c_void, out: *mut Info) -> u32;
    pub fn nf_native_result_generated(
        result: *const c_void,
        index: u64,
        out: *mut Generated,
    ) -> u32;
    pub fn nf_native_entities_same(a: *const c_void, b: *const c_void, out: *mut u32) -> u32;
    pub fn nf_native_result_release(result: *mut c_void);
}
