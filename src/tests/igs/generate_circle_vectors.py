#!/usr/bin/env python3
"""Maintainer-only O fixtures: independent geometry, rational spans and word bits.

Usage: python3 src/tests/igs/generate_circle_vectors.py /path/to/IGS
No SyncTERM output or Atari execution constructs expected pixels.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import hashlib
import itertools
import json
import re
import struct
import sys
from pathlib import Path

reference=Path(sys.argv[1]).resolve()
sys.path.insert(0,str(reference/'tools'))
from verify_tos104_gdp import closed_curve
from verify_tos104_curves import arithmetic
from verify_tos104_polygon import spans
from verify_tos104_line_clip import projected
from verify_tos104_lines import locations,bit_rule

table_source=(reference/'docs/igs/pattern-acceptance-data.adoc').read_text()
tables=json.JSONDecoder().raw_decode(table_source[table_source.index('{'):])[0]
user=[0,65535,0xa531,0x8001,0x5555,0xaaaa,0x0010,0x7ffe,0xf00f,0x00ff,0xff00,0x1357,0x2468,0x1111,0x8888,0x5aa5]
selections=[(0,1,[0]),(1,1,[65535])]
for interior,family,count,size,start in [(2,'DITHER',8,4,1),(2,'OEMPAT',16,8,9),(3,'HATCH0',6,8,1),(3,'HATCH1',6,16,7)]:
    rows=tables['tables']['_'+family]['words']
    selections += [(interior,start+i,rows[i*size:(i+1)*size]) for i in range(count)]
selections += [(4,1,user)]
cases=[]
def add(res,mode,selection,radius,perimeter,center=(80,80),clip=True,bounds=None,app=False):
    cases.append(dict(resolution=res,mode=mode,selection=selection,radius=radius,perimeter=perimeter,
        center=center,clipping=clip,bounds=bounds,last=bool((mode+radius)%2),app=app))
for res,mode,selection,radius,perimeter in itertools.product(range(3),range(1,5),range(39),(3,17),range(2)):
    add(res,mode,selection,radius,perimeter,app=radius==17 and selections[selection][:2] in ((1,1),(3,12)))
for res,mode,radius,perimeter in itertools.product(range(3),range(1,5),
        (0,1,2,3,4,5,7,15,31,63,127,128,131,132,255,256,507,508,511,512,515,516,-1,-2,-3,-17,-127),range(2)):
    add(res,mode,38,radius,perimeter)
for res,steps in itertools.product(range(3),range(32,129)):
    add(res,3,1,4*steps,1)
for res,mode,radius,perimeter,bounds in itertools.product(range(3),range(1,5),(17,132),range(2),
        ((50,45,105,95),(150,150,160,160),(0,150,20,160),(80,70,40,36))):
    add(res,mode,38,radius,perimeter,center=(80,60),bounds=bounds)
for res,mode,radius,perimeter,clip in itertools.product(range(3),range(1,5),(17,-3),range(2),(False,True)):
    add(res,mode,38,radius,perimeter,center=(3,4),clip=clip)
for res in range(3):
    yr=arithmetic(132,(338,169,372)[res],372)['result']
    for mode,perimeter in itertools.product(range(1,5),range(2)):
        add(res,mode,1,132,perimeter,center=(80,60),bounds=(0,60+yr,319 if res==0 else 639,60+yr))
    add(res,3,1,9999,1)


def oracle(case,phase):
    res=case['resolution'];planes=(4,2,1)[res];width=320 if res==0 else 640;height=400 if res==2 else 200
    x,y=case['center'];xr=case['radius'];yr=arithmetic(xr,(338,169,372)[res],372)['result']
    mode=case['mode']-1;_,_,rows=selections[case['selection']]
    words=[0 if phase==2 else (0x5aa5^((i%planes)*0x1357)) if phase==0 else (0xa55a^((i%planes)*0x2468)) for i in range(16000)]
    bounds=case['bounds'] or (0,0,width-1,height-1);clip=bounds if case['clipping'] else None
    if clip and (x+xr<clip[0] or x-xr>clip[2] or y+yr<clip[1] or y-yr>clip[3]):return words,int(case['last'])
    points=closed_curve(4,xr,yr,x,y);last=0
    if clip and (clip[0]>clip[2] or clip[1]>clip[3] or max(py for px,py in points)<clip[1] or min(py for px,py in points)>clip[3]):return words,last
    # Whole-word equations are independent of the renderer's per-pixel plot.
    # Source pattern words are absolute-X anchored in every native word group.
    for sy,left,right in spans(points,clip):
        if not 0<=sy<height:continue
        left=max(0,left);right=min(width-1,right)
        if left>right:continue
        pattern=rows[sy%len(rows)]
        for group in range(left//16,right//16+1):
            coverage=65535
            if group==left//16:coverage &= 65535>>(left%16)
            if group==right//16:coverage &= (65535<<(15-right%16))&65535
            for plane in range(planes):
                index=sy*width*planes//16+group*planes+plane;old=words[index];foreground=65535 if 5&(1<<plane) else 0
                if mode==0:value=foreground&pattern
                elif mode==1:value=(old&~pattern)|(foreground&pattern)
                elif mode==2:value=old^pattern
                else:value=(old&pattern)|(foreground&~pattern)
                words[index]=((old&~coverage)|(value&coverage))&65535
    if case['perimeter']:
        last=1
        for i,(a,b) in enumerate(zip(points,points[1:]+points[:1])):
            endpoints=(*a,*b)
            if clip:
                accepted,endpoints,_=projected(endpoints,clip)
                if not accepted:continue
            for px,py in locations(endpoints,mode,int(i==len(points)-1)):
                if not 0<=px<width or not 0<=py<height:continue
                for plane in range(planes):
                    index=py*width*planes//16+(px//16)*planes+plane;mask=32768>>(px%16)
                    value=bit_rule(mode,1,int(bool(words[index]&mask)),(5>>plane)&1)
                    words[index]=(words[index]&~mask)|(mask if value else 0)
    return words,last


def digest(words):
    value=14695981039346656037
    for word in words:
        for byte in (word>>8,word&255):value=((value^byte)*1099511628211)&0xffffffffffffffff
    return value


def runs(words,res):
    width=320 if res==0 else 640;height=400 if res==2 else 200;planes=(4,2,1)[res];result=[]
    for y in range(height):
        row=[sum(((words[y*width*planes//16+(x//16)*planes+p]>>(15-x%16))&1)<<p for p in range(planes)) for x in range(width)]
        start=0
        while start<width:
            end=start+1
            while end<width and row[end]==row[start]:end+=1
            if row[start]:result.append((y,start,end-start,row[start]))
            start=end
    return result


header=['/* Generated by src/tests/igs/generate_circle_vectors.py; see circle-vectors.json provenance. */','static const struct circle_case circle_cases[] = {']
application=[]
for i,case in enumerate(cases):
    hashes=[];lasts=[]
    for phase in range(2):
        words,last=oracle(case,phase);hashes.append(digest(words));lasts.append(last)
    assert lasts[0]==lasts[1]
    res=case['resolution'];interior,index,rows=selections[case['selection']]
    if case['app']:
        words,last=oracle(case,2)
        application.append(dict(resolution=res,mode=case['mode'],interior=interior,index=index,perimeter=case['perimeter'],
            center=case['center'],radius=case['radius'],last_line=last,runs=runs(words,res)))
    values=[res,case['mode'],interior,index,case['perimeter'],int(case['clipping']),int(case['last']),lasts[0],*case['center'],case['radius']]
    bounds=case['bounds'] or (0,0,319 if res==0 else 639,399 if res==2 else 199)
    header.append('\t{'+','.join(map(str,values))+',{'+','.join(map(str,bounds))+'},{UINT64_C(0x%016x),UINT64_C(0x%016x)}},'%tuple(hashes))
    if i%250==0:print(f'{i}/{len(cases)}',flush=True)
header.append('};')
models=['verify_tos104_gdp.py','verify_tos104_curves.py','verify_tos104_polygon.py','verify_tos104_line_clip.py','verify_tos104_lines.py']
sine=json.loads((reference/'docs/igs/generated/tos104-tables.json').read_text())['_sin_tbl']
sine_bytes=b''.join(struct.pack('>H',v) for v in sine['words'])
provenance=dict(generator='src/tests/igs/generate_circle_vectors.py',references=['15-graphics-commands.adoc','46-native-polygon-fill.adoc','48-native-curve-helpers.adoc','49-native-gdp-rounded-boxes.adoc','131-patterns-acceptance.adoc','138-delivery-curve-callers.adoc'],
    method='Independent native curve-point oracle, rational spans, whole-plane-word Boolean equations and ordered border pixels; no SyncTERM output or Atari execution.',
    model_sha256={n:hashlib.sha256((reference/'tools'/n).read_bytes()).hexdigest() for n in models},rom_sha256=tables['rom_sha256'],sine_table_sha256=hashlib.sha256(sine_bytes).hexdigest(),native_cases=len(cases),
    native_initial='primary0x5aa5 XOR plane*0x1357, alternate0xa55a XOR plane*0x2468',native_foreground='Effective logical fill pen7 retained across modes, physical5 masked to active depth',
    application_foreground='Wire C2,7 selects physical5 in low mode and logical1/active mask in medium/mono. Recolour zero-background runs to that independent pen result; XOR uses active mask.',
    safe_domain='Signed-radius native predicates on non-wrapping inputs; host writes discarded after native border traversal. Wide overflow corrections and extreme words have separate literal tests.')
root=Path(__file__).resolve().parents[2]
(root/'terminal/cterm/tests/data/igs_circle_vectors.h').write_text('\n'.join(header)+'\n')
encoded=json.dumps(dict(provenance=provenance,user=user,cases=application),indent=2)
encoded=re.sub(r'\[\s*(-?\d+(?:,\s*-?\d+)*)\s*\]',lambda m:'['+', '.join(re.findall(r'-?\d+',m.group(1)))+']',encoded)
(Path(__file__).parent/'circle-vectors.json').write_text(encoded+'\n')
print(f'Wrote {len(cases)} native frames and {len(application)} application cases.',flush=True)
