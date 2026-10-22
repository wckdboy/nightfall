# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
"""RFC 0013 URDF export, checked independently of the engine (gate G3).

Runs `nf mcp`, exports the six-joint arm, and drives the joints to several poses. At each
pose, forward kinematics computed here from the URDF alone (stdlib XML, no robotics library)
must reproduce every occurrence's solved placement within 1e-6 m (rotation entries within
1e-9). Masses must match `mass_properties`, meshes must exist and be in metres, and a closed
loop (the Strandbeest) must export as a tree with its loop-closing joints named.

Usage: urdf_tests.py <nf> <examples/assemblies>
"""
import json
import math
import os
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

NF, EXAMPLES = sys.argv[1], sys.argv[2]
failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


class Server:
    def __init__(self, document, folder):
        self.p = subprocess.Popen([NF, "mcp", "--in", document, "--allow-path", folder],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, text=True)
        self.n = 0
        self.rpc("initialize", {"protocolVersion": "2025-03-26", "capabilities": {},
                                "clientInfo": {"name": "urdf_tests", "version": "0"}})

    def rpc(self, method, params):
        self.n += 1
        self.p.stdin.write(json.dumps({"jsonrpc": "2.0", "id": self.n, "method": method,
                                       "params": params}) + "\n")
        self.p.stdin.flush()
        return json.loads(self.p.stdout.readline())

    def call(self, tool, **args):
        result = self.rpc("tools/call", {"name": tool, "arguments": args})["result"]
        return result.get("isError", False), result["structuredContent"]

    def close(self):
        self.p.stdin.close()
        self.p.wait()


def matmul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def origin(element):
    xyz, rpy = [0.0] * 3, [0.0] * 3
    if element is not None:
        xyz = [float(v) for v in element.get("xyz", "0 0 0").split()]
        rpy = [float(v) for v in element.get("rpy", "0 0 0").split()]
    r, p, y = rpy
    cr, sr, cp, sp = math.cos(r), math.sin(r), math.cos(p), math.sin(p)
    cy, sy = math.cos(y), math.sin(y)
    rot = [[cy * cp, cy * sp * sr - sy * cr, cy * sp * cr + sy * sr],
           [sy * cp, sy * sp * sr + cy * cr, sy * sp * cr - cy * sr],
           [-sp, cp * sr, cp * cr]]
    return [rot[0] + [xyz[0]], rot[1] + [xyz[1]], rot[2] + [xyz[2]], [0, 0, 0, 1]]


def motion(kind, axis, q):
    if kind in ("revolute", "continuous"):
        x, y, z = axis
        c, s, t = math.cos(q), math.sin(q), 1 - math.cos(q)
        return [[t * x * x + c, t * x * y - s * z, t * x * z + s * y, 0],
                [t * x * y + s * z, t * y * y + c, t * y * z - s * x, 0],
                [t * x * z - s * y, t * y * z + s * x, t * z * z + c, 0], [0, 0, 0, 1]]
    if kind == "prismatic":
        return [[1, 0, 0, axis[0] * q], [0, 1, 0, axis[1] * q], [0, 0, 1, axis[2] * q],
                [0, 0, 0, 1]]
    return [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]


def occurrence_frames(robot, values, root_pose):
    """World frame of each occurrence: link frames by forward kinematics, then the visual origin."""
    joints = robot.findall("joint")
    children = {j.find("child").get("link") for j in joints}
    roots = [l.get("name") for l in robot.findall("link") if l.get("name") not in children]
    check(len(roots) == 1, f"one root link, got {roots}")
    frames = {roots[0]: root_pose}
    pending = list(joints)
    while pending:
        progressed = False
        for j in list(pending):
            parent, child = j.find("parent").get("link"), j.find("child").get("link")
            if parent not in frames:
                continue
            axis = [float(v) for v in j.find("axis").get("xyz").split()] if j.find("axis") is not None else [1, 0, 0]
            frames[child] = matmul(matmul(frames[parent], origin(j.find("origin"))),
                                   motion(j.get("type"), axis, values.get(j.get("name"), 0.0)))
            pending.remove(j)
            progressed = True
        check(progressed, "every joint's parent is reachable")
        if not progressed:
            break
    return {l.get("name"): matmul(frames[l.get("name")], origin(l.find("visual/origin")))
            for l in robot.findall("link") if l.get("name") in frames}


def pose_matrix(o):
    r, t = o["rotation"], o["translation"]
    return [r[0:3] + [t[0] * 1e-3], r[3:6] + [t[1] * 1e-3], r[6:9] + [t[2] * 1e-3], [0, 0, 0, 1]]


with tempfile.TemporaryDirectory() as folder:
    folder = os.path.realpath(folder)
    arm = Server(os.path.join(EXAMPLES, "arm.nightfall"), folder)
    error, exported = arm.call("export_urdf", path=os.path.join(folder, "arm.urdf"))
    check(not error, f"export: {exported}")
    check(exported.get("cut_joints") == [], "a serial arm cuts no joint")
    robot = ET.parse(os.path.join(folder, "arm.urdf")).getroot()
    links = {l.get("name"): l for l in robot.findall("link")}
    check(len(links) == 7 and len(robot.findall("joint")) == 6, "seven links and six joints")
    types = {j.get("name"): j.get("type") for j in robot.findall("joint")}
    check(types["q2"] == "revolute" and types["q1"] == "continuous", f"joint types {types}")
    q2 = next(j for j in robot.findall("joint") if j.get("name") == "q2").find("limit")
    check(abs(float(q2.get("lower")) + 2 * math.pi / 3) < 1e-12 and
          abs(float(q2.get("upper")) - 2 * math.pi / 3) < 1e-12, "q2 limits in radians")
    # Masses agree with mass_properties; meshes exist, in metres.
    error, mass = arm.call("mass_properties")
    for o in mass["occurrences"]:
        urdf_mass = float(links[o["id"]].find("inertial/mass").get("value"))
        check(abs(urdf_mass - o["mass"]) <= 1e-12 * o["mass"], f"{o['id']} mass")
        mesh = os.path.join(folder, links[o["id"]].find("visual/geometry/mesh").get("filename"))
        check(os.path.exists(mesh), f"{mesh} exists")
        with open(mesh) as f:
            extent = max(abs(float(v)) for line in f if line.startswith("v ") for v in line.split()[1:])
        check(0.005 < extent < 0.5, f"{o['id']} mesh is in metres ({extent})")
    # Forward kinematics from the URDF alone against the solver at several poses.
    poses = [dict(q1=0, q2=0, q3=0, q4=0, q5=0, q6=0),
             dict(q1=30, q2=40, q3=-60, q4=20, q5=50, q6=10),
             dict(q1=-120, q2=-75, q3=110, q4=170, q5=-35, q6=-90),
             dict(q1=45, q2=90, q3=45, q4=-45, q5=90, q6=180)]
    worst_t = worst_r = 0.0
    for qs in poses:
        error, build = arm.call("set_parameters", values={k: f"{v} deg" for k, v in qs.items()})
        check(not error, f"pose {qs}: {build}")
        if error:
            continue
        solved = {o["id"]: o for o in build["assembly"]["occurrences"]}
        frames = occurrence_frames(robot, {k: math.radians(v) for k, v in qs.items()},
                                   pose_matrix(solved["base"]))
        for oid, o in solved.items():
            expected = pose_matrix(o)
            for i in range(3):
                worst_t = max(worst_t, abs(frames[oid][i][3] - expected[i][3]))
                for j in range(3):
                    worst_r = max(worst_r, abs(frames[oid][i][j] - expected[i][j]))
    check(worst_t <= 1e-6, f"translation within 1e-6 m (worst {worst_t})")
    check(worst_r <= 1e-9, f"rotation within 1e-9 (worst {worst_r})")
    arm.close()
    # A closed linkage exports as a tree: 12 loops, 12 joints cut and named.
    beest = Server(os.path.join(EXAMPLES, "strandbeest.nightfall"), folder)
    error, exported = beest.call("export_urdf", path=os.path.join(folder, "beest.urdf"))
    check(not error, f"strandbeest export: {exported}")
    robot = ET.parse(os.path.join(folder, "beest.urdf")).getroot()
    check(len(exported["cut_joints"]) == 12, f"12 loop joints cut: {exported['cut_joints']}")
    check(len(robot.findall("joint")) == 37 - 12 and len(robot.findall("link")) == 26,
          "a spanning tree of 26 links")
    beest.close()
    # A joint kind with no single URDF joint is refused with its pointer.
    blocks = os.path.join(folder, "blocks.nightfall")
    with open(blocks, "w") as f:
        json.dump({"format": "nightfall.part", "version": 2, "units": {"length": "mm", "angle": "deg"},
                   "meta": {}, "params": [], "references": [], "features": [], "history_end": None,
                   "ui": {}, "components": [{"id": "block", "features": [
                       {"id": "square", "type": "sketch", "inputs": {"plane": {"datum": "xy"}, "entities": [
                           {"id": "r", "type": "rect", "corner": [-5, -5], "size": [10, 10]}],
                           "constraints": []}},
                       {"id": "cube", "type": "extrude", "inputs": {"profile": {"sketch": "square"},
                        "distance": "10 mm", "result": {"new": "cube", "body_type": "brep"}}}]}],
                   "occurrences": [{"id": "base", "component": "block", "grounded": True},
                                   {"id": "top", "component": "block",
                                    "placement": {"translation": [0, 0, 11]}}],
                   "joints": [{"id": "spin", "kind": "cylindrical", "value": ["0 deg", "1 mm"],
                               "a": {"occurrence": "base", "frame": {"created_by": "cube", "role": "cap_end"}},
                               "b": {"occurrence": "top", "frame": {"created_by": "cube", "role": "cap_start"},
                                     "flip": True}}]}, f)
    cylinder = Server(blocks, folder)
    error, refused = cylinder.call("export_urdf", path=os.path.join(folder, "blocks.urdf"))
    check(error and refused["errors"][0]["pointer"] == "/joints/0/kind", f"cylindrical refused: {refused}")
    cylinder.close()
    print(f"URDF export: worst translation {worst_t:.3g} m, worst rotation entry {worst_r:.3g}")

for f in failures:
    print("FAILED:", f)
sys.exit(1 if failures else 0)
