#!/usr/bin/env python3
"""Import the historical RIPterm 1.54 primitive test captures (not SyncTERM output).

Usage: import_rip154_captures.py <legacy-rip_test-directory> <output-directory>
Only the recorded 640x350 TrueColor XWD layout is accepted. Expected pixels
retain exact six-bit DAC channels; this removes only the display's 0xAA versus
VGA's 0xA8 expansion of the same DAC value, with no spatial/difference tolerance.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

CASES = {
    "ellipses": {
        "test01_ellipses": "Thin/filled ellipses, circles, arcs and degenerate radii",
        "test01b_ellipse_sweep": "Small ellipse radius sweep",
        "test01c_large_ellipses": "Large ellipse integer truncation",
        "test01d_tall_ellipses": "Tall ellipse transition rows",
        "test01e_wide_sweep": "Wide ellipses and one-pixel vertical radius",
        "test01f_tall_sweep": "Tall ellipses and one-pixel horizontal radius",
        "test01g_xr2_sweep": "Two-pixel horizontal-radius sweep",
        "test01h_circles": "Circle radii and symmetry",
    },
    "raster": {
        "test02_lines": "Line octants, styles and thickness",
        "test03_floodfill": "Solid and patterned flood-fill boundaries",
        "test04_polygons": "Open/closed, filled and degenerate polygons",
        "test05_patterns": "Built-in and user fill patterns",
        "test06_xor": "XOR intersections and repeated drawing",
        "test07_pies": "Pie quadrants, partial arcs and ellipses",
        "test08_bezier": "Accumulated Bezier parameter error and thickness",
        "test09_rects": "Rectangle corners and fills",
        "test10_edges": "Zero-length lines, zero radii, thin polygons and reversed bounds",
        "test11_lines": "Line endpoint and tie-breaking sweep",
        "test12_fillpoly": "Polygon scan conversion, XOR and colour-zero outline suppression",
        "test13_thick_xor": "Three-pass thick-line XOR and Bezier start-point cancellation",
        "test14_thick_xor_ellipse": "Thick ellipse sweep and duplicate-point caps under XOR",
    },
    "buttons": {
        "test12_buttons": "Button style, state, label and orientation combinations",
        "test14_buttons_plain": "Plain-button orientations and label metrics",
        "test15_buttons_bevel": "Bevel extension and label placement",
        "test16_buttons_colors": "Button face, label, shadow and highlight colours",
        "test17_buttons_sizes": "Fixed/dynamic button dimensions and exclusive edges",
    },
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def decode_xwd(data):
    header = struct.unpack_from(">25I", data)
    size, version, fmt, depth, width, height, _, order = header[:8]
    assert (version, fmt, depth, width, height, header[11], header[12]) == (7, 2, 24, 640, 350, 32, 2560)
    assert header[14:17] == (0xff0000, 0xff00, 0xff)
    title = data[100:size].rstrip(b"\0").decode("ascii")
    assert "RIPTERM" in title and "syncterm" not in title.lower(), title
    offset = size + header[19] * 12
    assert offset + width * height * 4 == len(data)
    pixels = struct.unpack_from(("<" if order == 0 else ">") + str(width * height) + "I", data, offset)
    rgb6 = bytes((pixel >> shift) & 63 for pixel in pixels for shift in (18, 10, 2))
    return rgb6, title


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    rows = []
    for group, cases in CASES.items():
        for name, description in cases.items():
            wire = (args.source / (name + ".rip")).read_bytes()
            capture = (args.source / (name + ".xwd")).read_bytes()
            rgb6, title = decode_xwd(capture)
            # The original captures include a legacy text-window glyph and
            # DOS cursor, and these four include RIPterm's own status bar.
            # Record UI rectangles explicitly; retain the original pixel data.
            ui_rectangles = [] if name in ("test12_buttons", "test14_thick_xor_ellipse") else [[0, 0, 24, 8]]
            ui_context = []
            if ui_rectangles:
                # Verify the observed '$' ink and solid cursor, independently
                # of current SyncTERM output, before excluding their rectangle.
                for y, bits in enumerate((48, 124, 192, 120, 12, 248, 48, 0)):
                    for x in range(24):
                        if x >= 16 or (x < 8 and bits & (0x80 >> x)):
                            offset = (y * 640 + x) * 3
                            colour = b"\x3f\x3f\x3f"
                            if x >= 16 and name == "test03_floodfill":
                                colour = b"\0\0\x2a"
                            elif x >= 16 and name == "test16_buttons_colors" and y >= 5:
                                colour = b"\x15\x15\x15"
                            assert rgb6[offset:offset + 3] == colour, (name, x, y)
                ui_context.append("Legacy text-window '$' glyph and solid DOS text cursor")
            if name.startswith(("test14_buttons", "test15_", "test16_", "test17_")):
                ui_rectangles.append([0, 336, 640, 14])
                ui_context.append("RIPterm application status bar, not RIP scene output")
            if name in ("test03_floodfill", "test14_thick_xor_ellipse"):
                # Historical context: the isolated black bottom-right pixel is
                # the DOS mouse-pointer tip, not part of the RIP drawing.
                assert rgb6[-3:] == b"\0\0\0"
                ui_rectangles.append([639, 349, 1, 1])
                ui_context.append("Historical DOS mouse-pointer tip; absent in paired SyncTERM capture")
            (args.output / (name + ".rip")).write_bytes(wire)
            (args.output / (name + ".rgb6.z")).write_bytes(zlib.compress(rgb6, 9))
            rows.append(dict(id=name, group=group, description=description,
                             source_capture=name + ".xwd", source_title=title,
                             source_sha256=digest(capture), input_sha256=digest(wire),
                             rgb6_sha256=digest(rgb6), capture_ui_rectangles=ui_rectangles,
                             capture_ui_context=ui_context))
    manifest = dict(reference="Historical DOSBox RIPterm 1.54 XWD captures; rip_test/ellipse_research.md and git history",
                    width=640, height=350, pixel_format="top-down row-major RGB, one byte per six-bit DAC channel, zlib",
                    cases=rows)
    (args.output / "captures.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Imported {len(rows)} independent reference captures")


if __name__ == "__main__":
    main()
