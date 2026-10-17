// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// OCCT half of the pipeline test, in its own translation unit (RFC 0002: OCCT's Handle
// macro breaks OpenUSD headers).

#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepTools.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>

#include <string>

// A 10 × 20 × 30 box with one edge filleted at r = 2, written as an OCCT .brep. Returns
// OCCT's volume, or a negative value on failure.
double write_filleted_box_brep(const std::string& path) {
  const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape();
  TopTools_IndexedMapOfShape edges;
  TopExp::MapShapes(box, TopAbs_EDGE, edges);
  BRepFilletAPI_MakeFillet fillet(box);
  fillet.Add(2.0, TopoDS::Edge(edges(1)));
  fillet.Build();
  if (!fillet.IsDone() || !BRepTools::Write(fillet.Shape(), path.c_str())) return -1.0;
  GProp_GProps props;
  BRepGProp::VolumeProperties(fillet.Shape(), props, 1e-9, true);
  return props.Mass();
}
