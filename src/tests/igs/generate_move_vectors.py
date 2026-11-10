#!/usr/bin/env python3
"""Maintainer import of source-defined m scenes and original console rows.

Usage: python3 -B src/tests/igs/generate_move_vectors.py /path/to/IGS
Normal tests use the checked-in data, without external assets or emulator output.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import hashlib
import itertools
import json
from pathlib import Path
import sys

root=Path(sys.argv[1]).resolve();source=root/'docs/igs/generated/tos104-fonts.json'
fonts=json.loads(source.read_text());cases=[]
for res,state,selector in itertools.product(range(3),range(7),range(6)):
    cases.append(dict(resolution=res,state=state,selector=selector,count=(0,2,3)[(state+selector)%3],
                      reverse=(state+selector)%2==1,background=2,wrap=state==6))
for res,pair in itertools.product(range(3),((0,0),(1,0),(2,25),(3,80),(4,80),(5,2),(1,9999))):
    cases.append(dict(resolution=res,state=0,selector=pair[0],count=pair[1],reverse=False,background=2,wrap=False))
assert len(cases)==147
data=dict(provenance=dict(generator='src/tests/igs/generate_move_vectors.py',
    references=['14-console-commands.adoc','66-native-console-pixels.adoc','195-delivery-console.adoc'],
    method='Source-defined signed selector/repetition and persistent console-state composition; independent native algebra/state interpreter and application oracle, with literal original TOS font rows. No emulator-generated expectations.',
    font_export_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
    rom_sha256=hashlib.sha256((root/'TOS/tos104.img').read_bytes()).hexdigest(),
    native_normal_cases=70404,native_composition_cases=1764,application_cases=len(cases)),
    fonts={str(f['form_height']):{str(c):[int(row.replace('.','0').replace('#','1'),2) for row in f['glyphs'][c]] for c in (65,66,67,68,72)} for f in fonts[1:]},cases=cases)
(Path(__file__).parent/'move.json').write_text(json.dumps(data,indent=2)+'\n')
print('Wrote147 source-defined m inputs and original console font rows.')
