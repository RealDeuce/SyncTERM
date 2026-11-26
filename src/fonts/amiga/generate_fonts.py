#!/usr/bin/env python3
"""Embed pinned native Amiga files unchanged; runtime uses the common HUNK decoder."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import hashlib
import json
from pathlib import Path
import sys


def main():
    manifest, output = map(Path, sys.argv[1:])
    fonts = json.loads(manifest.read_text(encoding='utf-8'))['fonts']
    text = ['/* Generated from pinned bitmap sources; do not edit. */',
            '#include "amiga_font_internal.h"']
    entries = []
    masks = {'1.3': 1, '2.x+': 2}
    for index, entry in enumerate(fonts):
        path = manifest.parent / entry['file']
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != entry['sha256']:
            raise ValueError(f'Native font identity mismatch: {path}')
        text.append(f'static const uint8_t file_{index}[] = {{')
        for offset in range(0, len(data), 24):
            text.append(','.join(f'0x{value:02x}' for value in data[offset:offset+24]) + ',')
        text.append('};')
        mask = sum(masks[name] for name in entry['sets'])
        entries.append('{' + json.dumps(entry['name']) +
                       f', {entry["height"]}, {mask}, file_{index}, sizeof(file_{index}), NULL' + '}')
    text.append('struct st_amiga_builtin st_amiga_builtins[] = {')
    text.extend(entry + ',' for entry in entries)
    text.append('};')
    text.append('const size_t st_amiga_builtins_count = sizeof(st_amiga_builtins)/sizeof(st_amiga_builtins[0]);')
    with output.open('w', encoding='utf-8', newline='\n') as stream:
        stream.write('\n'.join(text) + '\n')


if __name__ == '__main__':
    main()
