# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
"""Embed a service and its pinned OCCT dylib closure; ad-hoc sign a development app.

Usage: python3 tools/embed_service.py host.app kernel.xpc OCCT_INSTALL_PREFIX
Only the copied service/libraries and the supplied build-output host are modified.
This is development packaging under RFC 0003, not distribution signing/notarization.
"""
import plistlib
import re
import shutil
import subprocess
import sys
from pathlib import Path


def run(*args):
    result = subprocess.run([str(a) for a in args], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f"{args[0]} failed: {result.stderr.strip()}")
    return result.stdout


def executable(bundle):
    with (bundle / "Contents/Info.plist").open("rb") as stream:
        return bundle / "Contents/MacOS" / plistlib.load(stream)["CFBundleExecutable"]


def dependencies(binary):
    return [line.strip().split(" (compatibility version", 1)[0]
            for line in run("/usr/bin/otool", "-L", binary).splitlines()[1:]]


def system(path):
    return path.startswith(("/System/Library/", "/usr/lib/"))


def clean_rpaths(binary, path):
    commands = run("/usr/bin/otool", "-l", binary)
    paths = re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset", commands)
    for old in dict.fromkeys(paths):
        run("/usr/bin/install_name_tool", "-delete_rpath", old, binary)
    run("/usr/bin/install_name_tool", "-add_rpath", path, binary)


def remove_build_metadata(binary):
    # CMake's Swift driver emits dependency files beside the executable. MacOS is code-only.
    for metadata in binary.parent.glob("*.d"):
        metadata.unlink()


def embed(app, service, occt):
    app, service, occt = app.resolve(), service.resolve(), occt.resolve()
    if app.suffix != ".app" or service.suffix != ".xpc":
        raise ValueError("expected an existing build-output .app and .xpc")
    host = executable(app)
    if any(not system(dep) for dep in dependencies(host)):
        raise ValueError("host must not link the kernel or other non-system libraries")
    destination = app / "Contents/XPCServices/NightfallKernel.xpc"
    if service == destination:
        raise ValueError("source service must be outside the destination app")
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        shutil.rmtree(destination)
    shutil.copytree(service, destination)
    frameworks = destination / "Contents/Frameworks"
    frameworks.mkdir(parents=True, exist_ok=True)
    binary = executable(destination)
    remove_build_metadata(binary)
    remove_build_metadata(host)
    queue, copied = [binary], {}
    while queue:
        current = queue.pop()
        for dep in dependencies(current):
            if system(dep):
                continue
            name = Path(dep).name
            source = (occt / "lib" / name).resolve()
            if not name.startswith("libTK") or not source.is_relative_to(occt / "lib"):
                raise ValueError(f"unexpected non-system dependency: {dep}")
            if name not in copied:
                target = frameworks / name
                shutil.copy2(source, target)
                copied[name] = target
                queue.append(target)
            if dep != f"@rpath/{name}":
                run("/usr/bin/install_name_tool", "-change", dep, f"@rpath/{name}", current)
    for name, library in copied.items():
        run("/usr/bin/install_name_tool", "-id", f"@rpath/{name}", library)
        clean_rpaths(library, "@loader_path")
        run("/usr/bin/codesign", "--force", "--sign", "-", library)
    resources = destination / "Contents/Resources"
    resources.mkdir(exist_ok=True)
    for name in ("LICENSE_LGPL_21.txt", "OCCT_LGPL_EXCEPTION.txt"):
        shutil.copy2(Path(__file__).resolve().parents[1] / "third_party/src/occt" / name, resources / name)
    clean_rpaths(binary, "@executable_path/../Frameworks")
    clean_rpaths(host, "/usr/lib/swift")
    run("/usr/bin/codesign", "--force", "--sign", "-", destination)
    run("/usr/bin/codesign", "--force", "--sign", "-", app)
    run("/usr/bin/codesign", "--verify", "--deep", "--strict", app)
    print(f"Embedded {len(copied)} OCCT dylibs in {app.name}; ad-hoc signatures verified")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    embed(*(Path(arg) for arg in sys.argv[1:]))
