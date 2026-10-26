// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <expected>
#include <string>

#include "nf/kernel/kernel.hpp"

namespace nf::study {
/// Writes a solid as a BREP file for the mesher.
std::expected<void, kernel::KernelError> write_brep(const kernel::ShapePtr& solid,
                                                    const std::string& path);
/// The 1-based position of `face` in OCCT's TopExp face map of its owning solid: the number
/// the mesher tags boundary triangles with.
std::expected<int, kernel::KernelError> face_number(const kernel::EntityPtr& face);
/// The number of faces of a solid.
std::expected<int, kernel::KernelError> face_count(const kernel::ShapePtr& solid);
}  // namespace nf::study
