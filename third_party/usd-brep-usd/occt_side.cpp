// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
//
// SPIKE (RFC 0002): OCCT half. Kept in its own translation unit because OCCT's `Handle`
// macro breaks OpenUSD headers included after it.

#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <GProp_GProps.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Reader.hxx>

#include <string>

bool step_to_brep(const std::string& step, const std::string& brep, double& volume) {
  STEPControl_Reader reader;
  if (reader.ReadFile(step.c_str()) != IFSelect_RetDone || reader.TransferRoots() == 0) {
    return false;
  }
  const TopoDS_Shape shape = reader.OneShape();
  GProp_GProps props;
  BRepGProp::VolumeProperties(shape, props, 1e-9, true);
  volume = props.Mass();
  return BRepTools::Write(shape, brep.c_str());
}
