# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
"""Check the shipped Mach-O closure/signatures, then exercise relocated embedded services."""
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def run(*args):
    try:
        return subprocess.check_output([str(arg) for arg in args], text=True, stderr=subprocess.STDOUT,
                                       timeout=30)
    except subprocess.CalledProcessError as error:
        print(error.output)
        raise


def binary(bundle):
    with (bundle / "Contents/Info.plist").open("rb") as stream:
        return bundle / "Contents/MacOS" / plistlib.load(stream)["CFBundleExecutable"]


def verify(app):
    service = app / "Contents/XPCServices/NightfallKernel.xpc"
    with (service / "Contents/Info.plist").open("rb") as stream:
        info = plistlib.load(stream)
    assert info["CFBundleIdentifier"] == "dev.nightfall.Nightfall.Kernel"
    assert info["CFBundlePackageType"] == "XPC!"
    assert info["XPCService"]["ServiceType"] == "Application"
    libraries = list((service / "Contents/Frameworks").glob("*.dylib"))
    names = {library.name for library in libraries}
    assert "libTKernel.7.9.dylib" in names
    assert not (app / "Contents/Frameworks").exists(), "kernel libraries leaked into host"
    for image in [binary(app), binary(service), *libraries]:
        assert not image.is_symlink(), "library points outside the bundle"
        run("/usr/bin/codesign", "--verify", "--strict", image)
        deps = run("/usr/bin/otool", "-L", image).splitlines()[1:]
        for dep in deps:
            value = dep.strip().split(" (compatibility version", 1)[0]
            if value.startswith(("/usr/lib/", "/System/Library/")):
                continue
            assert image != binary(app), "host links non-system code"
            assert value.startswith("@rpath/") and Path(value).name in names, value
        commands = run("/usr/bin/otool", "-l", image)
        paths = re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset", commands)
        expected = "/usr/lib/swift" if image == binary(app) else (
            "@executable_path/../Frameworks" if image == binary(service) else "@loader_path")
        assert paths == [expected], (image, paths)
        assert re.search(r"minos 15\.0\b", commands), "unexpected deployment target"
    run("/usr/bin/codesign", "--verify", "--deep", "--strict", app)
    return len(libraries)


with tempfile.TemporaryDirectory(prefix="nightfall bundle with spaces ") as directory:
    packager = Path(directory) / "tools/embed_service.py"
    packager.parent.mkdir()
    shutil.copy2(Path(__file__).resolve().parents[2] / "tools/embed_service.py", packager)
    for source, modes in [(Path(sys.argv[1]), ["smoke", "references", "restart"]),
                          (Path(sys.argv[2]), ["crash", "hang"])]:
        app = Path(directory) / source.name
        shutil.copytree(source, app)
        # Reproduce a cached CI install: the packager's source tree contains no OCCT checkout.
        print(run(sys.executable, packager, app,
                  source / "Contents/XPCServices/NightfallKernel.xpc", sys.argv[4]), end="")
        count = verify(app)
        print(f"{app.name}: verified {count} service-local dylibs and relocated signatures")
        for mode in modes:
            print(run(binary(app), mode, sys.argv[3]), end="")
