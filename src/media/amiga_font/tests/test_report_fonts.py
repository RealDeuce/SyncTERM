#!/usr/bin/env python3
"""Pinned report font coverage and independent native metrics/ink hashes."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import json
from pathlib import Path
import subprocess
import sys

reference = json.loads((Path(__file__).resolve().parents[1] /
                        'data/report-font-reference.json').read_text(encoding='utf-8'))
actual = {}
for row in subprocess.check_output([sys.argv[1], '--builtins'], text=True, encoding='utf-8').splitlines():
    name, height, width, baseline, digest = row.split('\t')
    actual[name.lower(), int(height)] = (int(width), int(baseline), digest)
assert len(reference['fonts']) == 38
assert len({(font['name'], font['height']) for font in reference['fonts']}) == 38
assert sum('raster_fnv64' in font for font in reference['fonts']) == 20
for font in reference['fonts']:
    key = font['name'], font['height']
    assert key in actual, ('Missing reported font', key)
    if 'raster_fnv64' in font:
        expected = font['width'], font['baseline'], font['raster_fnv64']
        assert actual[key] == expected, (key, actual[key], expected)
print('All 38 reported family/size pairs are bundled; 20 matching native strikes have exact metrics/ink hashes.')
