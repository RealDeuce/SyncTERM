#!/usr/bin/env python3
"""Compare C output with the user's pinned ROM-execution vectors, without ROMs."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
vectors=json.loads((Path(__file__).parent/'data/skypix/golden-vectors.json').read_text())
def probe(*args):
    return subprocess.check_output([sys.argv[1],*map(str,args)],text=True).splitlines()
def points(lines):
    return [tuple(map(int,line.split())) for line in lines]
for case in vectors['line_examples']:
    assert points(probe('line',*case['from'],*case['to']))==list(map(tuple,case['points'])),case
for case in vectors['ellipse_examples']:
    variant=13 if case['rom']=='ks13' else 31
    actual=points(probe('ellipse',variant,*case['center'],*case['radii']))
    digest=hashlib.sha256(b''.join(struct.pack('>hh',*p) for p in actual)).hexdigest()
    assert len(actual)==case['writes'] and digest==case['sha256'],(case,len(actual),digest)
for rom,case in vectors['text_overhang_example'].items():
    result=probe('text',13 if rom=='ks13' else 31)
    assert int(result[0])==case['final_x'],case
    assert sorted(points(result[1:]))==list(map(tuple,case['points'])),(rom,result)
