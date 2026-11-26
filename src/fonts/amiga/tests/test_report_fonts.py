#!/usr/bin/env python3
"""Every native built-in, in both profiles, matches independently decoded inputs."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import hashlib
import json
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1] / 'data'
reference = json.loads((root / 'native-reference.json').read_text(encoding='utf-8'))
manifest = json.loads((root / 'manifest.json').read_text(encoding='utf-8'))
for font in manifest['fonts']:
    assert hashlib.sha256((root / font['file']).read_bytes()).hexdigest() == font['sha256']
for version, option in [('1.3', '--builtins-13'), ('2.x+', '--builtins')]:
    actual = {}
    for row in subprocess.check_output([sys.argv[1], option], text=True, encoding='utf-8').splitlines():
        name, height, width, baseline, digest = row.split('\t')
        actual[name.lower(), int(height)] = (int(width), int(baseline), digest)
    expected = {(font['name'], font['height']): (font['width'], font['baseline'], font['raster_fnv64'])
                for font in reference['fonts'] if version in font['sets']}
    assert len(expected) == 35
    assert actual == expected, (version, {key: (actual.get(key), value)
                                         for key, value in expected.items() if actual.get(key) != value})
print('All 65 native files verified: all 35 strikes in each Kickstart profile have exact metrics/ink hashes.')
