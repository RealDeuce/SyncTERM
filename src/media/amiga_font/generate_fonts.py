#!/usr/bin/env python3
"""Compile the pinned YAFF bitmap subset into immutable native font data."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import hashlib
import json
from pathlib import Path
import re
import sys

def read_font(path):
    global_props, glyphs, labels, rows, props = {}, [], [], [], {}
    def finish():
        nonlocal labels, rows, props
        if labels:
            glyphs.append((labels, rows, props))
        labels, rows, props = [], [], {}
    for line in path.read_text().splitlines():
        if not line or line.lstrip().startswith('#'):
            continue
        if not line[0].isspace():
            if line.endswith(':'):
                if rows or props:
                    finish()
                labels.append(line[:-1])
            elif ':' in line:
                k, v = line.split(':', 1)
                global_props[k] = v.strip()
        else:
            value = line.strip()
            if ':' in value:
                k, v = value.split(':', 1)
                props[k] = v.strip()
            elif re.fullmatch(r'[.@-]+', value):
                rows.append('' if value == '-' else value)
    finish()
    return global_props, glyphs

def compile_font(entry, directory, index):
    path = directory / entry['file']
    assert hashlib.sha256(path.read_bytes()).hexdigest() == entry['sha256'], path
    gp, raw = read_font(path)
    width = int(gp.get('cell-size', gp.get('bounding-box', '8x8')).split('x')[0])
    baseline = entry["height"]+int(gp.get("shift-up","0"))-1
    adjustment = baseline-entry.get("baseline",baseline)
    baseline = entry.get("baseline",baseline)
    data, glyphs = [], []
    for labels, rows, props in raw:
        w = max((len(row) for row in rows), default=0)
        stride = (w+7)//8
        offset = len(data)
        for row in rows:
            bits = row.ljust(w, '.')
            for col in range(0, w, 8):
                data.append(sum((1 << (7-j)) for j,c in enumerate(bits[col:col+8]) if c == '@'))
        bearing = int(props.get('left-bearing', gp.get('left-bearing', '0')))
        right = int(props.get('right-bearing', gp.get('right-bearing', '0')))
        advance = bearing+w+right if gp.get('spacing') == 'proportional' else width
        top = -int(props.get('shift-up', gp.get('shift-up', '0')))+adjustment
        glyphs.append((labels, (offset,stride,w,len(rows),bearing,advance,top)))
    fallback = next((v for labels,v in glyphs if 'default' in labels), (0,0,0,0,0,width,0))
    mapping = [fallback]*256
    for prefix in ['u+', '0x']:
        for labels, value in glyphs:
            for label in labels:
                if label.startswith(prefix):
                    try: code = int(label[len(prefix):],16)
                    except ValueError: continue
                    if code < 256: mapping[code] = value
    text = f'static const uint8_t bits_{index}[] = {{'+','.join(map(str,data or [0]))+'};\n'
    text += f'static const struct st_amiga_font font_{index} = {{.height={entry["height"]}, .baseline={max(0,baseline)}, .glyphs={{\n'
    for off,stride,w,h,b,a,t in mapping:
        text+=f'{{bits_{index}+{off},{stride},{w},{h},{b},{a},{t},0}},\n'
    return text+'}};\n'

def main():
    manifest, output = map(Path, sys.argv[1:])
    fonts=json.loads(manifest.read_text())['fonts']
    text='/* Generated from pinned bitmap sources; do not edit. */\n#include "amiga_font_internal.h"\n'
    for index, entry in enumerate(fonts): text+=compile_font(entry, manifest.parent, index)
    text+='const struct st_amiga_builtin st_amiga_builtins[] = {\n'
    for index, entry in enumerate(fonts): text+='{'+json.dumps(entry['name'])+f', &font_{index}'+'},\n'
    text+='};\nconst size_t st_amiga_builtins_count = sizeof(st_amiga_builtins)/sizeof(st_amiga_builtins[0]);\n'
    output.write_text(text)
if __name__=='__main__': main()
