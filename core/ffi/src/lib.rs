// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//! The plain-C boundary of Nightfall's Rust solvers, linked into the C++ evaluator as one
//! static library: `nf_sketch_solve` (`include/nf/sketch/solve.h`, RFC 0012) and
//! `nf_assembly_solve` (`include/nf/assembly/solve.h`, RFC 0013). Only [`abi`] contains unsafe
//! code: it turns caller-owned pointer/count pairs into slices after checking them, which is
//! the audited C ABI boundary RFC 0008 allows. A panic is caught there and reported, never
//! unwound into C.
#![deny(unsafe_op_in_unsafe_fn)]
#![deny(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

mod abi;
pub mod assembly;
pub mod sketch;
