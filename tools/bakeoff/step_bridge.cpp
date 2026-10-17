// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0

#include "step_bridge.hpp"

#include <BRepTools.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Reader.hxx>
#include <Standard_Failure.hxx>

namespace nf::bakeoff {

bool step_to_brep(const std::string& step_path, const std::string& brep_path, std::string& error) {
  try {
    STEPControl_Reader reader;
    if (reader.ReadFile(step_path.c_str()) != IFSelect_RetDone || reader.TransferRoots() == 0) {
      error = "STEP read failed";
      return false;
    }
    if (!BRepTools::Write(reader.OneShape(), brep_path.c_str())) {
      error = ".brep write failed";
      return false;
    }
    return true;
  } catch (const Standard_Failure& e) {
    error = e.GetMessageString();
    return false;
  }
}

}  // namespace nf::bakeoff
