#!/usr/bin/env python3
"""Maintainer import of independent p inputs and original console font rows.

Usage: python3 -B src/tests/igs/generate_position_vectors.py /path/to/IGS
Normal tests use the checked-in data; no SyncTERM output supplies expectations.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import hashlib
import itertools
import json
from pathlib import Path
import sys

root=Path(sys.argv[1]).resolve();source=root/'docs/igs/generated/tos104-fonts.json'
fonts=json.loads(source.read_text());cases=[]
pairs=((3,2),(300,400),(224,224),(95,96),(233,234),(66,251),(67,251),(57,251))
for res,state,pair,reverse in itertools.product(range(3),range(7),pairs,(False,True)):
    cases.append(dict(resolution=res,state=state,column=pair[0],row=pair[1],reverse=reverse,background=2,wrap=state==6))
for res,pair in itertools.product(range(3),((0,0),(39,24),(40,25),(79,23),(80,24),(223,255),(256,256),(9999,9999))):
    cases.append(dict(resolution=res,state=0,column=pair[0],row=pair[1],reverse=False,background=2,wrap=False))
codes={65,89}
for column,row in pairs:
    codes.update(byte for byte in ((row+32)%256,(column+32)%256) if byte>=32)
assert len(cases)==360
data=dict(provenance=dict(generator='src/tests/igs/generate_position_vectors.py',
    references=['14-console-commands.adoc','66-native-console-pixels.adoc','195-delivery-console.adoc'],
    method='Source-defined unscaled signed-word/low-byte console composition, independently interpreted by native and application tests; literal original font rows. No SyncTERM-generated expected output.',
    font_export_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
    rom_sha256=hashlib.sha256((root/'TOS/tos104.img').read_bytes()).hexdigest(),
    native_word_cases=157252,native_composition_cases=4452,native_byte_cases=6144,application_cases=len(cases)),
    fonts={str(f['form_height']):{str(c):[int(row.replace('.','0').replace('#','1'),2) for row in f['glyphs'][c]] for c in sorted(codes)} for f in fonts[1:]},cases=cases)
(Path(__file__).parent/'position.json').write_text(json.dumps(data,indent=2)+'\n')
print('Wrote 360 source-derived p cases and literal console font rows.')
