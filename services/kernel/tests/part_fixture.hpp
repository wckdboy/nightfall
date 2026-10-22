// SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "nf/ir/document.hpp"

namespace nf::kernel_service::test {
inline ir::Feature feature(std::string id, std::string type, const char* inputs) {
  ir::Feature f;
  f.id = std::move(id);
  f.type = std::move(type);
  f.inputs = ir::json::parse(inputs).value();
  return f;
}
inline ir::Document part(std::size_t count = 3) {
  ir::Document d;
  d.params = {{"depth", "30 mm", {}, {}, {}, {}},
              {"step", "3 mm", {}, {}, {}, {}},
              {"unused", 1.0, {}, {}, {}, {}}};
  d.features.push_back(feature("outline", "sketch", R"({"plane":{"datum":"xy"},"entities":[
    {"id":"r","type":"rect","corner":[0,0],"size":[10,20]}],"constraints":[]})"));
  d.features.push_back(feature("base", "extrude", R"({"profile":{"sketch":"outline"},
    "distance":"depth","result":{"new":"plate","body_type":"brep"}})"));
  for (std::size_t i = 2; i < count; ++i)
    d.features.push_back(feature("move_" + std::to_string(i), "transform",
                                 R"({"bodies":["plate"],"translation":["step",0,0]})"));
  return d;
}
inline std::string source(const ir::Document& d) { return ir::write_document(d).value(); }
/// Appends a `fixed` constraint for every entity, after the existing constraints so their
/// indices and pointers are unchanged. The solver (RFC 0012) then cannot move anything, so a
/// test of the constraint checker's tolerances and diagnostics keeps its meaning.
inline ir::json::Value pinned(const ir::json::Value& inputs) {
  ir::json::Object result = *inputs.if_object();
  for (auto& member : result)
    if (member.key == "constraints") {
      ir::json::Array constraints = *member.value.if_array();
      for (const auto& entity : *inputs.find("entities")->if_array())
        constraints.push_back(
            ir::json::Object{{"type", "fixed"}, {"entities", ir::json::Array{*entity.find("id")}}});
      member.value = std::move(constraints);
    }
  return result;
}
}  // namespace nf::kernel_service::test
