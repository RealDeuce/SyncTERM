#!/usr/bin/env python3
"""Maintainer-only X13 decimal scenes from independent original-font oracles.

Usage: python3 -B src/tests/igs/generate_math_vectors.py /path/to/IGS
Ordinary tests read the checked-in JSON, without ROMs or SyncTERM expectations.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import hashlib
import json
import re
from pathlib import Path
import sys

from filled_curve_oracle import runs
from text_oracle import NativeText

reference = Path(sys.argv[1]).resolve()
model = NativeText(reference)
inputs = (
    ("X>13,0,0,0:", "0", 0, 0, 9, 1),
    ("X>13,1,9999,9999,0:", "19998", 0, 1, 18, 2),
    ("X>13,2,0,7,0:", "-7", 0, 4, 10, 3),
    ("X>13,3,9999,9999,0:", "-27935", 0, 8, 8, 1),
    ("X>13,2,0,7,0:X>13,4,0],3,0:", "-2", 0, 16, 16, 2),
    ("X>13,2,0,7,0:X>13,5,0],3,0:", "-1", 0, 31, 20, 3),
    ("X>13,0,37,127:X>13,6,128,9999:", "0", 127, 0, 9, 1),
    ("", "0", 0, 0, 9, 1),
)
cases = []
for resolution in range(3):
    for number, (wire, text, index, style, point, mode) in enumerate(inputs):
        origin = (160 if resolution == 0 else 320, 200 if resolution == 2 else 100)
        case = dict(resolution=resolution, mode=mode, points=point, style=style,
                    rotation=number % 4, origin=origin, clipping=True, bounds=None,
                    last=True, align=(0, 0), text=list(text.encode("ascii")))
        frame = model.frame(case, 0, application=True)
        cases.append(dict(number=number, wire=wire, index=index, decimal=text,
                          **{k: case[k] for k in ("resolution", "mode", "points", "style", "rotation", "origin")},
                          runs=runs(frame, resolution)))
fonts = {}
for height, font in ((8, model.fonts[1]), (16, model.fonts[2])):
    fonts[str(height)] = {c: [int("".join("1" if b == "#" else "0" for b in row), 2)
                            for row in font["glyphs"][ord(c)]] for c in "-0123456789AZ"}
prior = json.loads((Path(__file__).parent / "text-vectors.json").read_text())["provenance"]
for name, expected in prior["model_sha256"].items():
    assert hashlib.sha256((reference / "tools" / name).read_bytes()).hexdigest() == expected
assert hashlib.sha256((reference / "TOS/tos104.img").read_bytes()).hexdigest() == prior["rom_sha256"]
provenance = dict(generator="src/tests/igs/generate_math_vectors.py",
                  references=["19-extended-math-sweeps.adoc", "139-delivery-text-callers.adoc",
                              "140-delivery-shared-text-readers.adoc", "144-text-acceptance.adoc"],
                  method="Literal signed decimal results; independent original-font glyph/effects and line oracles. No SyncTERM output.",
                  fonts_sha256=hashlib.sha256((reference / "docs/igs/generated/tos104-fonts.json").read_bytes()).hexdigest(),
                  rom_sha256=prior["rom_sha256"], model_sha256=prior["model_sha256"])
encoded = json.dumps(dict(provenance=provenance, fonts=fonts, cases=cases), indent=2)
encoded = re.sub(r'\[\s*(-?\d+(?:,\s*-?\d+)*)\s*\]',
                 lambda m: '[' + ', '.join(re.findall(r'-?\d+', m.group(1))) + ']', encoded)
(Path(__file__).parent / "math-vectors.json").write_text(encoded + "\n")
