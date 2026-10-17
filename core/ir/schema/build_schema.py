# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
"""Maintain the RFC 0005 schema in reusable definitions; publish deterministic JSON.

Run with --check in CI. x-nf-reference/definition annotate semantic links and
x-nf-value marks field dimensions; ordinary JSON Schema validators ignore them.
"""
import json
from pathlib import Path
import sys


def ref(name):
    return {"$ref": "#/$defs/" + name}


def obj(properties, required=None):
    return {"type": "object", "properties": properties,
            "required": list(properties) if required is None else required,
            "patternProperties": {"^x-": {}}, "additionalProperties": False}


def array(items, minimum=0, maximum=None):
    result = {"type": "array", "items": items, "minItems": minimum}
    if maximum is not None:
        result["maxItems"] = maximum
    return result


def choice(*schemas):
    return {"anyOf": list(schemas)}


def link(kind, schema=None):
    return {**(ref("id") if schema is None else schema), "x-nf-reference": kind}


def one_field(schema, *fields):
    return {**schema, "oneOf": [{"required": [f]} for f in fields]}


def numeric(kind):
    return {**ref("value"), "x-nf-value": kind}


defs = {
    "id": {"type": "string", "pattern": r"^[a-z_][a-z0-9_]{0,63}(?![\s\S])"},
    "entity_id": {"type": "string", "pattern": r"^[a-z_][a-z0-9_]{0,63}/[a-z_][a-z0-9_]{0,63}(?![\s\S])"},
    "text": {"type": "string"}, "nonempty": {"type": "string", "minLength": 1},
    "value": choice({"type": "number"}, ref("nonempty")),
    "count": {**choice({"type": "integer", "minimum": 1}, ref("nonempty")), "x-nf-value": "count"},
    "point2": array(numeric("length"), 2, 2), "point3": array(numeric("length"), 3, 3),
    "direction": choice({"enum": ["x", "y", "z", "-x", "-y", "-z"]}, array(numeric("scalar"), 3, 3)),
    "units": obj({"length": {"enum": ["mm", "cm", "m", "in", "ft"]}, "angle": {"enum": ["deg", "rad"]}}),
    "meta": obj({"name": ref("text"), "doc": ref("text")}, []),
    "parameter": obj({"name": ref("id"), "value": ref("value"), "min": ref("value"),
                      "max": ref("value"), "doc": ref("text")}, ["name", "value"]),
    "reference": obj({"id": ref("id"), "kind": ref("nonempty"), "path": ref("nonempty"),
                      "sha256": {"type": "string", "pattern": r"^[0-9a-fA-F]{64}(?![\s\S])"}}),
    "result": {"oneOf": [obj({"new": {**ref("id"), "x-nf-definition": "body"},
                               "body_type": {"enum": ["brep", "mesh", "field"]}}),
                           *[obj({op: link("body")}) for op in ["join", "cut", "intersect"]]]},
    "where": obj({"type": {"enum": ["plane", "cylinder", "cone", "sphere", "torus", "nurbs", "line", "circle"]},
                  **{key: ref("direction") for key in ["normal", "parallel_to", "perpendicular_to"]},
                  "near": ref("point3"), "radius": numeric("length"),
                  "convex": {"type": "boolean"}, "concave": {"type": "boolean"}}, []),
}
selector_options = {"where": ref("where"), "expect": {"enum": ["one", "some", "any"]}, "except": ref("selector")}
defs["selector"] = {"oneOf": [
    obj({"created_by": link("feature"), "role": ref("nonempty"), "from": link("entity", ref("entity_id")),
         **selector_options}, ["created_by"]),
    obj({"edges_of": ref("selector"), **selector_options}, ["edges_of"]),
    obj({"faces_of": link("body"), **selector_options}, ["faces_of"]),
    obj({"any": array(ref("selector"), 1), **selector_options}, ["any"])]}
defs["plane"] = choice(obj({"datum": {"enum": ["xy", "yz", "zx"]}}), ref("selector"),
                       obj({"origin": ref("point3"), "normal": ref("direction"), "x_direction": ref("direction")}))
defs["sketch_entity"] = {**obj({"sketch": link("sketch"), "entity": ref("id")}), "x-nf-reference": "sketch_entity"}
defs["axis"] = choice({**ref("sketch_entity"), "x-nf-reference": "sketch_line"}, ref("selector"),
                      obj({"origin": ref("point3"), "direction": ref("direction")}))
defs["path"] = choice(ref("sketch_entity"), ref("selector"))
defs["profile"] = obj({"sketch": link("sketch"), "regions": array(ref("point2"), 1),
                       "fill_rule": {"enum": ["evenodd", "nonzero"]}}, ["sketch"])
defs["local_entity"] = choice(link("local_entity"), obj({"entity": link("local_entity"),
                                                       "point": {"enum": ["start", "end", "center"]}}))
entity_inputs = {
    "point": {"at": ref("point2")},
    "line": {"start": ref("point2"), "end": ref("point2")},
    "arc": {"center": ref("point2"), "radius": numeric("length"), "start_angle": numeric("angle"), "sweep_angle": numeric("angle")},
    "circle": {"center": ref("point2"), "radius": numeric("length")},
    "ellipse": {"center": ref("point2"), "radii": ref("point2"), "rotation": numeric("angle")},
    "rect": {"corner": ref("point2"), "size": ref("point2")},
    "polygon": {"points": array(ref("point2"), 3)},
    "spline": {"degree": {**choice({"type": "integer", "minimum": 1, "maximum": 9}, ref("nonempty")), "x-nf-value": "degree"},
               "knots": array(numeric("scalar"), 4), "control_points": array(ref("point2"), 2), "weights": array(numeric("scalar"), 2)},
    "equation_curve": {"x": ref("nonempty"), "y": ref("nonempty"), "t0": numeric("scalar"), "t1": numeric("scalar"), "tolerance": numeric("length")},
    "svg": {"reference": link("svg"), "placement": obj({"origin": ref("point2"), "scale": numeric("scalar"), "rotation": numeric("angle")}, [])}}
entity_optional = {"ellipse": ["rotation"], "spline": ["weights"], "svg": ["placement"]}
for kind, fields in entity_inputs.items():
    defs["entity_" + kind] = obj({"id": {**ref("id"), "x-nf-definition": "entity"}, "type": {"const": kind}, **fields},
                                 ["id", "type"] + [f for f in fields if f not in entity_optional.get(kind, [])])
defs["entity"] = {"oneOf": [ref("entity_" + kind) for kind in entity_inputs]}
constraint_sizes = {"coincident": 2, "horizontal": 1, "vertical": 1, "parallel": 2, "perpendicular": 2,
                    "tangent": 2, "equal": 2, "symmetric": 3, "midpoint": 2, "fixed": 1,
                    "distance": 2, "angle": 2, "radius": 1}
for kind, size in constraint_sizes.items():
    fields = {"type": {"const": kind}, "entities": array(ref("local_entity"), size, size)}
    if kind in ["distance", "angle", "radius"]:
        fields["value"] = numeric("angle" if kind == "angle" else "length")
    defs["constraint_" + kind] = obj(fields)
defs["constraint"] = {"oneOf": [ref("constraint_" + kind) for kind in constraint_sizes]}
defs["sketch"] = obj({"plane": ref("plane"), "entities": array(ref("entity")), "constraints": array(ref("constraint"))})
defs["extrude"] = one_field(obj({"profile": ref("profile"), "distance": numeric("length"), "to": ref("selector"),
                                "direction": ref("direction"), "symmetric": {"type": "boolean"},
                                "draft": numeric("angle"), "result": ref("result")}, ["profile", "result"]), "distance", "to")
defs["revolve"] = obj({"profile": ref("profile"), "axis": ref("axis"), "angle": numeric("angle"), "result": ref("result")})
defs["sweep"] = obj({"profile": ref("profile"), "path": ref("path"), "result": ref("result")})
defs["loft"] = obj({"profiles": array(ref("profile"), 2), "rails": array(ref("path"), 1), "result": ref("result")}, ["profiles", "result"])
defs["fillet"] = obj({"edges": ref("selector"), "radius": numeric("length")})
defs["chamfer"] = obj({"edges": ref("selector"), "distance": numeric("length"), "angle": numeric("angle")}, ["edges", "distance"])
defs["shell"] = obj({"body": link("body"), "faces": ref("selector"), "thickness": numeric("length")})
defs["hole"] = {**obj({"face": ref("selector"), "positions": array({**ref("sketch_entity"), "x-nf-reference": "sketch_point"}, 1),
                       "diameter": numeric("length"), "depth": numeric("length"),
                       "counterbore": obj({"diameter": numeric("length"), "depth": numeric("length")}),
                       "countersink": obj({"diameter": numeric("length"), "angle": numeric("angle")})},
                      ["face", "positions", "diameter", "depth"]), "not": {"required": ["counterbore", "countersink"]}}
defs["boolean"] = obj({"op": {"enum": ["union", "subtract", "intersect"]}, "target": link("body"),
                       "tools": array(link("body"), 1), "keep_tools": {"type": "boolean"}}, ["op", "target", "tools"])
pattern_targets = {"features": array(link("feature"), 1), "bodies": array(link("body"), 1)}
for kind, fields in {"pattern_linear": {"count": ref("count"), "spacing": numeric("length"), "direction": ref("direction")},
                     "pattern_circular": {"count": ref("count"), "angle": numeric("angle"), "axis": ref("axis")},
                     "mirror": {"plane": ref("plane")}}.items():
    defs[kind] = one_field(obj({**pattern_targets, **fields}, list(fields)), "features", "bodies")
defs["transform"] = {**obj({"bodies": array(link("body"), 1), "translation": ref("point3"),
                            "rotation": obj({"axis": ref("axis"), "angle": numeric("angle")})}, ["bodies"]),
                     "anyOf": [{"required": ["translation"]}, {"required": ["rotation"]}]}
defs["import"] = obj({"reference": link("reference"), "result": ref("result")})
defs["args"] = {"type": "object", "patternProperties": {"^x-": {}, defs["id"]["pattern"]: ref("value")}, "additionalProperties": False}
defs["script"] = obj({"source": ref("nonempty"), "args": ref("args"), "result": ref("result")})
feature_kinds = ["sketch", "extrude", "revolve", "sweep", "loft", "fillet", "chamfer", "shell", "hole",
                 "boolean", "pattern_linear", "pattern_circular", "mirror", "transform", "import", "script"]
for kind in feature_kinds:
    defs["feature_" + kind] = obj({"id": ref("id"), "type": {"const": kind}, "name": ref("text"),
                                  "suppressed": {"type": "boolean"}, "inputs": ref(kind)}, ["id", "type", "inputs"])
defs["feature"] = {"oneOf": [ref("feature_" + kind) for kind in feature_kinds]}
schema = {"$schema": "https://json-schema.org/draft/2020-12/schema",
          "$id": "https://github.com/wckdboy/nightfall/raw/main/core/ir/schema/part-v1.schema.json",
          "$comment": "Generated by build_schema.py. SPDX-License-Identifier: Apache-2.0. RFC 0005.",
          "title": "Nightfall parametric part v1", "$defs": defs,
          **obj({"format": {"const": "nightfall.part"}, "version": {"const": 1}, "units": ref("units"),
                 "meta": ref("meta"), "params": array(ref("parameter")), "references": array(ref("reference")),
                 "features": array(ref("feature")), "history_end": choice({"type": "null"}, ref("id")), "ui": {"type": "object"}})}
output = json.dumps(schema, indent=2, ensure_ascii=False) + "\n"
path = Path(__file__).with_name("part-v1.schema.json")
if "--check" in sys.argv:
    if not path.exists() or path.read_text() != output:
        sys.exit("Schema is stale; run python3 core/ir/schema/build_schema.py")
else:
    path.write_text(output)
