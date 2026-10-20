// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// Smoke tests for the OCCT build: modeling and STEP round-trip with analytic answers.

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <NCollection_IncAllocator.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <Standard_OutOfMemory.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pnt.hxx>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <numbers>
#include <optional>
#include <vector>

using Catch::Matchers::WithinRel;

namespace {

constexpr double kRelTol = 1e-6;

double volume(const TopoDS_Shape& shape) {
  GProp_GProps props;
  BRepGProp::VolumeProperties(shape, props);
  return props.Mass();
}

bool is_valid(const TopoDS_Shape& shape) { return BRepCheck_Analyzer(shape).IsValid(); }

// The box edge running from (0,0,0) to (0,0,height).
std::optional<TopoDS_Edge> z_edge_at_origin(const TopoDS_Shape& box) {
  for (TopExp_Explorer it(box, TopAbs_EDGE); it.More(); it.Next()) {
    const TopoDS_Edge& edge = TopoDS::Edge(it.Current());
    bool at_origin_axis = true;
    for (TopExp_Explorer v(edge, TopAbs_VERTEX); v.More(); v.Next()) {
      const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(v.Current()));
      at_origin_axis = at_origin_axis && std::abs(p.X()) < 1e-9 && std::abs(p.Y()) < 1e-9;
    }
    if (at_origin_axis) return edge;
  }
  return std::nullopt;
}

}  // namespace

TEST_CASE("incremental allocator aligns mixed allocations and preserves payloads across resets",
          "[occt][occt-allocator]") {
  NCollection_IncAllocator allocator(2048);
  struct alignas(std::max_align_t) Payload {
    std::uint64_t id;
    double value;
  };
  struct Bytes {
    unsigned char* address;
    std::size_t size;
    unsigned char value;
  };
  for (int round = 0; round < 4; ++round) {
    std::vector<Bytes> buffers;
    std::vector<Payload*> objects;
    for (std::size_t i = 0; i < 512; ++i) {
      const std::size_t size = 1 + (i * 17) % 251;
      auto* p = static_cast<unsigned char*>(i % 2 ? allocator.Allocate(size)
                                                  : allocator.AllocateOptimal(size));
      REQUIRE(p);
      REQUIRE(reinterpret_cast<std::uintptr_t>(p) % alignof(std::max_align_t) == 0);
      const auto value = static_cast<unsigned char>(i % 256);
      std::fill_n(p, size, value);
      buffers.push_back({p, size, value});
      if (i % 16 == 0) {
        void* storage = allocator.Allocate(sizeof(Payload));
        REQUIRE(reinterpret_cast<std::uintptr_t>(storage) % alignof(Payload) == 0);
        objects.push_back(
            new (storage) Payload{static_cast<std::uint64_t>(i), static_cast<double>(i) + 0.25});
      }
    }
    for (const auto& buffer : buffers)
      CHECK(std::all_of(buffer.address, buffer.address + buffer.size,
                        [&](unsigned char v) { return v == buffer.value; }));
    for (std::size_t i = 0; i < objects.size(); ++i) {
      CHECK(objects[i]->id == i * 16);
      CHECK(objects[i]->value == static_cast<double>(i * 16) + 0.25);
      objects[i]->~Payload();
    }
    allocator.Reset(round % 2 == 1);
  }
}

TEST_CASE("incremental allocator rejects unrepresentable sizes before allocation",
          "[occt][occt-allocator]") {
  NCollection_IncAllocator allocator;
  for (const auto size :
       {std::numeric_limits<std::size_t>::max(), std::numeric_limits<std::size_t>::max() - 1,
        static_cast<std::size_t>(std::numeric_limits<unsigned int>::max())}) {
    CHECK_THROWS_AS(allocator.Allocate(size), Standard_OutOfMemory);
    CHECK_THROWS_AS(allocator.AllocateOptimal(size), Standard_OutOfMemory);
  }
  void* first = allocator.Allocate(0);
  void* second = allocator.AllocateOptimal(0);
  REQUIRE(first);
  REQUIRE(second);
  CHECK(first != second);
  CHECK(reinterpret_cast<std::uintptr_t>(first) % alignof(std::max_align_t) == 0);
  CHECK(reinterpret_cast<std::uintptr_t>(second) % alignof(std::max_align_t) == 0);
}

TEST_CASE("box volume is exact", "[occt]") {
  const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape();
  REQUIRE(is_valid(box));
  CHECK_THAT(volume(box), WithinRel(6000.0, kRelTol));
}

TEST_CASE("filleting one box edge removes (1 - pi/4) r^2 L", "[occt]") {
  constexpr double r = 2.0, length = 30.0;
  const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 20.0, length).Shape();
  const auto edge = z_edge_at_origin(box);
  REQUIRE(edge.has_value());

  BRepFilletAPI_MakeFillet fillet(box);
  fillet.Add(r, *edge);
  fillet.Build();
  REQUIRE(fillet.IsDone());
  const TopoDS_Shape result = fillet.Shape();
  REQUIRE(is_valid(result));

  const double expected = 6000.0 - (1.0 - std::numbers::pi / 4.0) * r * r * length;
  CHECK_THAT(volume(result), WithinRel(expected, kRelTol));
}

TEST_CASE("a through-hole removes pi r^2 h", "[occt]") {
  // The same operation that exposes SMLIB-001 in SMLib (docs/known-issues).
  const TopoDS_Shape plate = BRepPrimAPI_MakeBox(12.0, 12.0, 1.5).Shape();
  const TopoDS_Shape tool =
      BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(1.5, 1.5, -1.0), gp::DZ()), 0.75, 3.5).Shape();
  BRepAlgoAPI_Cut cut(plate, tool);
  REQUIRE(cut.IsDone());
  REQUIRE(is_valid(cut.Shape()));
  const double expected = 12.0 * 12.0 * 1.5 - std::numbers::pi * 0.75 * 0.75 * 1.5;
  CHECK_THAT(volume(cut.Shape()), WithinRel(expected, kRelTol));
}

TEST_CASE("STEP round-trip preserves a filleted solid", "[occt][step]") {
  const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape();
  const auto edge = z_edge_at_origin(box);
  REQUIRE(edge.has_value());
  BRepFilletAPI_MakeFillet fillet(box);
  fillet.Add(2.0, *edge);
  const TopoDS_Shape original = fillet.Shape();

  STEPControl_Writer writer;
  REQUIRE(writer.Transfer(original, STEPControl_AsIs) == IFSelect_RetDone);
  REQUIRE(writer.Write("occt_smoke_roundtrip.step") == IFSelect_RetDone);

  STEPControl_Reader reader;
  REQUIRE(reader.ReadFile("occt_smoke_roundtrip.step") == IFSelect_RetDone);
  REQUIRE(reader.TransferRoots() > 0);
  const TopoDS_Shape loaded = reader.OneShape();
  REQUIRE(is_valid(loaded));
  CHECK_THAT(volume(loaded), WithinRel(volume(original), kRelTol));
}

// RFC 0001 "operation history": can every face and edge of a result be traced to the input
// sub-shape or operation that produced it? Traced = an input sub-shape kept unchanged, any
// image from Modified(), or anything Generated() from an input sub-shape.
namespace {

template <typename Builder>
int untraced(Builder& builder, const std::vector<TopoDS_Shape>& inputs, TopAbs_ShapeEnum kind) {
  TopTools_IndexedMapOfShape traced;
  for (const TopoDS_Shape& input : inputs) {
    for (const TopAbs_ShapeEnum sub : {TopAbs_FACE, TopAbs_EDGE, TopAbs_VERTEX}) {
      TopTools_IndexedMapOfShape subs;
      TopExp::MapShapes(input, sub, subs);
      for (int i = 1; i <= subs.Extent(); ++i) {
        const TopoDS_Shape& s = subs(i);
        if (!builder.IsDeleted(s) && builder.Modified(s).IsEmpty()) traced.Add(s);
        for (const TopoDS_Shape& m : builder.Modified(s)) traced.Add(m);
        for (const TopoDS_Shape& g : builder.Generated(s)) traced.Add(g);
      }
    }
  }
  TopTools_IndexedMapOfShape result;
  TopExp::MapShapes(builder.Shape(), kind, result);
  int missing = 0;
  for (int i = 1; i <= result.Extent(); ++i) missing += traced.Contains(result(i)) ? 0 : 1;
  return missing;
}

}  // namespace

TEST_CASE("OCCT traces every face, but not every edge, through a fillet", "[occt][history]") {
  const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape();
  const auto edge = z_edge_at_origin(box);
  REQUIRE(edge.has_value());
  BRepFilletAPI_MakeFillet fillet(box);
  fillet.Add(2.0, *edge);
  fillet.Build();
  REQUIRE(fillet.IsDone());
  CHECK(fillet.Generated(*edge).Extent() == 1);  // the new cylindrical face
  CHECK(untraced(fillet, {box}, TopAbs_FACE) == 0);
  // Measured gap (RFC 0001 F12): the fillet reports face history but not edge history, so
  // OCCT scores "partial". If this starts failing, OCCT improved: rescore history.
  CHECK(untraced(fillet, {box}, TopAbs_EDGE) > 0);
}

TEST_CASE("OCCT traces every face and edge through a boolean cut", "[occt][history]") {
  const TopoDS_Shape plate = BRepPrimAPI_MakeBox(12.0, 12.0, 1.5).Shape();
  const TopoDS_Shape tool =
      BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(6, 6, -1), gp::DZ()), 1.0, 3.5).Shape();
  BRepAlgoAPI_Cut cut(plate, tool);
  REQUIRE(cut.IsDone());
  CHECK(untraced(cut, {plate, tool}, TopAbs_FACE) == 0);
  CHECK(untraced(cut, {plate, tool}, TopAbs_EDGE) == 0);
}
