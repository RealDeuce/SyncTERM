#!/usr/bin/env python3
# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause
"""Check CMake's generated macOS metadata without requiring an Apple SDK."""

import os
from pathlib import Path
import plistlib
import subprocess
import sys


def run(*args):
    subprocess.run(args, check=True)


def main():
    cmake, test_root = sys.argv[1:]
    fixture = Path(__file__).resolve().parent / "macos_bundle_fixture"
    root = Path(test_root).resolve()
    if sys.platform != "darwin":
        native = root / "native"
        run(cmake, "-S", str(fixture), "-B", str(native),
            "-DCMAKE_BUILD_TYPE=Release")
        product = Path((native / "product-Release.txt").read_text().strip())
        assert product.name == ("syncterm.exe" if os.name == "nt" else "syncterm")
        assert ".app" not in product.parts
        # Visual Studio generators cannot generate Darwin build rules.
        if os.name == "nt":
            return
    # Generate Darwin build rules using the native C compiler. No Darwin
    # binary is built or executed on a non-Apple host.
    darwin = root / "darwin"
    options = ["-DCMAKE_BUILD_TYPE=Release"]
    if sys.platform != "darwin":
        options += ["-DCMAKE_SYSTEM_NAME=Darwin",
                    "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY"]
    run(cmake, "-S", str(fixture), "-B", str(darwin),
        *options)
    bundle = Path((darwin / "product-Release.txt").read_text().strip())
    assert bundle.name == "SyncTERM.app", bundle
    if sys.platform == "darwin":
        # Xcode stages the plist and resources during the build.
        run(cmake, "--build", str(darwin), "--config", "Release")
    with (bundle / "Contents/Info.plist").open("rb") as stream:
        info = plistlib.load(stream)
    expected = {
        "CFBundleExecutable": "SyncTERM",
        "CFBundleIdentifier": "net.bbsdev.syncterm",
        "CFBundleName": "SyncTERM",
        "CFBundleDisplayName": "SyncTERM",
        "CFBundleIconFile": "SyncTERM.icns",
        "CFBundlePackageType": "APPL",
        "CFBundleShortVersionString": "2.0.0",
        "CFBundleVersion": "2.0.0a1",
        "CFBundleLongVersionString": "2.0α",
        "NSHighResolutionCapable": True,
        "NSSupportsAutomaticGraphicsSwitching": True,
        "NSPrincipalClass": "NSApplication",
    }
    for key, value in expected.items():
        assert info[key] == value, (key, info.get(key))
    assert "2.0α" in info["CFBundleGetInfoString"]
    assert "2026" in info["NSHumanReadableCopyright"]
    assert "NSMainNibFile" not in info
    schemes = {scheme for entry in info["CFBundleURLTypes"]
               for scheme in entry["CFBundleURLSchemes"]}
    assert schemes == {"ssh", "telnet", "rlogin"}, schemes
    assert "${" not in (bundle / "Contents/Info.plist").read_text()
    icon = fixture.parents[2] / "resources/macos/SyncTERM.icns"
    data = icon.read_bytes()
    assert data[:4] == b"icns" and int.from_bytes(data[4:8], "big") == len(data)
    if sys.platform == "darwin":
        assert (bundle / "Contents/Resources/SyncTERM.icns").read_bytes() == data
        assert os.access(bundle / "Contents/MacOS/SyncTERM", os.X_OK)


if __name__ == "__main__":
    main()
