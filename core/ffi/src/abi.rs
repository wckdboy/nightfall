// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//! The audited unsafe boundary: pointer/count pairs from C become slices here and nowhere
//! else. Each entry point checks every array, catches panics and writes an error index.
use crate::assembly;
use crate::sketch::*;
use std::panic::{AssertUnwindSafe, catch_unwind};

/// Slice from a C array that may be null only when `count` is zero.
///
/// # Safety
/// When non-null, `ptr` must point to `count` initialized, aligned values that stay valid
/// and unaliased by writes for the duration of the call.
unsafe fn input<'a, T>(ptr: *const T, count: usize) -> Option<&'a [T]> {
    if count == 0 {
        return Some(&[]);
    }
    if ptr.is_null()
        || count
            .checked_mul(size_of::<T>())
            .is_none_or(|bytes| bytes > isize::MAX as usize)
    {
        return None;
    }
    // SAFETY: non-null, size checked, validity and lifetime are the caller's contract above.
    Some(unsafe { std::slice::from_raw_parts(ptr, count) })
}

/// Mutable slice from a C output array, under the same rules as [`input`].
///
/// # Safety
/// As [`input`], and no other reference may access the array during the call.
unsafe fn output<'a, T>(ptr: *mut T, count: usize) -> Option<&'a mut [T]> {
    if count == 0 {
        return Some(&mut []);
    }
    if ptr.is_null()
        || count
            .checked_mul(size_of::<T>())
            .is_none_or(|bytes| bytes > isize::MAX as usize)
    {
        return None;
    }
    // SAFETY: as above, with exclusive access guaranteed by the caller.
    Some(unsafe { std::slice::from_raw_parts_mut(ptr, count) })
}

/// See `nf_sketch_solve` in `include/nf/sketch/solve.h`.
///
/// # Safety
/// Every non-null pointer must satisfy the header's contract: arrays of the stated counts,
/// valid for the call; `report` and `error_index` point to writable values or are null.
#[unsafe(no_mangle)]
#[allow(clippy::too_many_arguments)]
pub unsafe extern "C" fn nf_sketch_solve(
    entities: *const CEntity,
    entity_count: usize,
    constraints: *const CConstraint,
    constraint_count: usize,
    solved: *mut CEntity,
    underconstrained: *mut u8,
    failing: *mut u8,
    redundant: *mut u8,
    report: *mut CReport,
    error_index: *mut usize,
) -> u32 {
    // SAFETY: each conversion checks null/size; validity is the caller's contract.
    let arrays = unsafe {
        (
            input(entities, entity_count),
            input(constraints, constraint_count),
            output(solved, entity_count),
            output(underconstrained, entity_count),
            output(failing, constraint_count),
            output(redundant, constraint_count),
            report.as_mut(),
        )
    };
    let (status, index) = match arrays {
        (
            Some(e),
            Some(c),
            Some(solved),
            Some(underconstrained),
            Some(failing),
            Some(redundant),
            Some(report),
        ) => {
            let out = Outputs {
                solved,
                underconstrained,
                failing,
                redundant,
                report,
            };
            match catch_unwind(AssertUnwindSafe(|| solve_c(e, c, out))) {
                Ok(Ok(())) => (OK, usize::MAX),
                Ok(Err(failure)) => failure,
                Err(_) => (INTERNAL, usize::MAX),
            }
        }
        _ => (INVALID_ARGUMENT, usize::MAX),
    };
    // SAFETY: null or a writable usize, per the contract.
    if let Some(slot) = unsafe { error_index.as_mut() } {
        *slot = index;
    }
    status
}

/// See `nf_assembly_solve` in `include/nf/assembly/solve.h`.
///
/// # Safety
/// Every non-null pointer must satisfy the header's contract: arrays of the stated counts,
/// valid for the call; `report` and `error_index` point to writable values or are null.
#[unsafe(no_mangle)]
#[allow(clippy::too_many_arguments)]
pub unsafe extern "C" fn nf_assembly_solve(
    occurrences: *const assembly::COccurrence,
    occurrence_count: usize,
    joints: *const assembly::CJoint,
    joint_count: usize,
    placements: *mut assembly::CPlacement,
    residual_mm: *mut f64,
    residual_rad: *mut f64,
    redundant: *mut u8,
    conflicting: *mut u8,
    report: *mut assembly::CReport,
    error_index: *mut usize,
) -> u32 {
    // SAFETY: each conversion checks null/size; validity is the caller's contract.
    let arrays = unsafe {
        (
            input(occurrences, occurrence_count),
            input(joints, joint_count),
            output(placements, occurrence_count),
            output(residual_mm, joint_count),
            output(residual_rad, joint_count),
            output(redundant, joint_count),
            output(conflicting, joint_count),
            report.as_mut(),
        )
    };
    let (status, index) = match arrays {
        (
            Some(o),
            Some(j),
            Some(placements),
            Some(residual_mm),
            Some(residual_rad),
            Some(redundant),
            Some(conflicting),
            Some(report),
        ) => {
            let out = assembly::Outputs {
                placements,
                residual_mm,
                residual_rad,
                redundant,
                conflicting,
                report,
            };
            match catch_unwind(AssertUnwindSafe(|| assembly::solve_c(o, j, out))) {
                Ok(Ok(())) => (assembly::OK, usize::MAX),
                Ok(Err(failure)) => failure,
                Err(_) => (assembly::INTERNAL, usize::MAX),
            }
        }
        _ => (assembly::INVALID_ARGUMENT, usize::MAX),
    };
    // SAFETY: null or a writable usize, per the contract.
    if let Some(slot) = unsafe { error_index.as_mut() } {
        *slot = index;
    }
    status
}
