// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// OCCT half of RFC 0014 meshing: no Netgen or MFEM headers here.
#include "geometry.hpp"

#include <BRepTools.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>

#include "snapshot.hpp"

namespace nf::study {
namespace {
std::unexpected<kernel::KernelError> error(std::string message) {
  return std::unexpected(kernel::KernelError{"study", std::move(message)});
}
}  // namespace

std::expected<void, kernel::KernelError> write_brep(const kernel::ShapePtr& solid,
                                                    const std::string& path) {
  const auto source = std::dynamic_pointer_cast<const kernel::detail::Snapshot>(solid);
  if (!source) return error("solid belongs to an incompatible backend");
  if (!BRepTools::Write(source->geometry, path.c_str())) return error("BREP write failed");
  return {};
}

std::expected<int, kernel::KernelError> face_number(const kernel::EntityPtr& face) {
  const auto entity = std::dynamic_pointer_cast<const kernel::detail::Subshape>(face);
  if (!entity || entity->kind() != kernel::EntityKind::kFace)
    return error("not a face of this backend");
  TopTools_IndexedMapOfShape faces;
  TopExp::MapShapes(entity->snapshot->geometry, TopAbs_FACE, faces);
  const int index = faces.FindIndex(entity->geometry);
  if (index < 1) return error("face is not in its owning solid");
  return index;
}

std::expected<int, kernel::KernelError> face_count(const kernel::ShapePtr& solid) {
  const auto source = std::dynamic_pointer_cast<const kernel::detail::Snapshot>(solid);
  if (!source) return error("solid belongs to an incompatible backend");
  TopTools_IndexedMapOfShape faces;
  TopExp::MapShapes(source->geometry, TopAbs_FACE, faces);
  return faces.Extent();
}
}  // namespace nf::study
