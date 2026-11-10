#!/usr/bin/env python3
"""Maintainer-only U outline fixtures from independent TOS geometry/bit oracles.

Usage: python3 src/tests/igs/generate_round_box_vectors.py /path/to/IGS
Ordinary tests use checked-in data. No SyncTERM output or Atari execution.
Positive-U frames reuse B's independently derived rounded-fill fixtures.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import hashlib
import itertools
import json
import re
import sys
from pathlib import Path

reference=Path(sys.argv[1]).resolve()
sys.path.insert(0,str(reference/'tools'))
from verify_tos104_gdp import rounded
from verify_tos104_wide import shaft_ops
from verify_tos104_polygon import spans
from verify_tos104_line_clip import projected
from verify_tos104_lines import locations,bit_rule

cases=[]
def add(res,mode,style,width,caps,points=(33,32,93,74),clip=True,last=True,app=False,text_after=False):
    order=(style+mode)%4
    x1,y1,x2,y2=points
    points=(x2 if order&1 else x1,y2 if order&2 else y1,x1 if order&1 else x2,y1 if order&2 else y2)
    cases.append(dict(resolution=res,mode=mode,style=style,width=width,caps=caps,points=points,
                      clipping=clip is not False,bounds=None if isinstance(clip,bool) else clip,last=last,app=app,text_after=text_after))
for res,mode,style,begin,end in itertools.product(range(3),range(1,5),range(1,8),range(3),range(3)):
    add(res,mode,style,1,(begin,end),last=bool(style%2),app=(begin,end)==(1,2) and style in (1,3,7))
for res,mode,width,style,caps in itertools.product(range(3),range(1,5),(3,9,39),(1,3,7),((0,0),(1,1),(2,2))):
    add(res,mode,style,width,caps,last=False,app=width in (3,9) and style==1 and caps==(1,1))
for res,width in itertools.product(range(3),range(3,40,2)):
    add(res,1+width%4,1,width,(1,2),last=False)
for res,mode,width,(w,h) in itertools.product(range(3),range(1,5),(1,3,9),
        ((0,0),(1,1),(2,2),(3,3),(7,2),(15,1),(1,15),(18,18),(60,0),(0,42),(12,7))):
    add(res,mode,7 if width==1 else 1,width,(1,1),points=(33,32,33+w,32+h),last=False)
for res,mode,width,clip in itertools.product(range(3),range(1,5),(1,3,9,39),
        ((40,36,88,70),(40,100,88,110),(120,36,130,70),(80,70,40,36),False)):
    add(res,mode,3,width,(1,1),clip=clip,last=True)
for res,mode,width,points in itertools.product(range(3),range(1,5),(1,9),((-4,2,15,20),(310,190,325,205))):
    add(res,mode,3,width,(1,1),points=points,clip=False,last=False)
for res,mode,width in itertools.product(range(3),range(1,5),(1,9)):
    ch=16 if res==2 else 8
    add(res,mode,3 if width==1 else 1,width,(1,1),points=(90,18*ch-4,120,18*ch+14),app=True,text_after=True)


def oracle(case,phase):
    res=case['resolution'];planes=(4,2,1)[res];width=320 if res==0 else 640;height=400 if res==2 else 200
    mode=case['mode']-1;bounds=case['bounds'] or (0,0,width-1,height-1);clip=bounds if case['clipping'] else None
    invalid=bool(clip and (clip[0]>clip[2] or clip[1]>clip[3]))
    words=[0 if phase==2 else (0x5aa5^((i%planes)*0x1357)) if phase==0 else (0xa55a^((i%planes)*0x2468)) for i in range(16000)]
    points=rounded(case['points'],width-1,(338,169,372)[res],372)[0]
    def paint(x,y,p):
        if not 0<=x<width or not 0<=y<height:return
        for plane in range(planes):
            index=y*width*planes//16+(x//16)*planes+plane;mask=32768>>(x%16)
            value=bit_rule(mode,p,int(bool(words[index]&mask)),(9>>plane)&1)
            words[index]=(words[index]&~mask)|(mask if value else 0)
    def line(a,b,ending,limits,pattern=65535):
        if limits:
            if limits[0]>limits[2] or limits[1]>limits[3]:return pattern
            accepted,endpoints,_=projected((*a,*b),limits)
            if not accepted:return pattern
        else:endpoints=(*a,*b)
        pixels=locations(endpoints,mode,ending);horizontal=endpoints[1]==endpoints[3]
        for j,(x,y) in enumerate(pixels):paint(x,y,(pattern>>(15-((x if horizontal else j)&15)))&1)
        count=len(pixels)%16
        return ((pattern<<count)|(pattern>>(16-count)))&65535 if count else pattern
    if case['width']==1:
        pattern=(65535,65520,0xc0c0,0xff18,0xff00,0xf191,65535 if phase==2 else 0xa531)[case['style']-1]
        for i,(a,b) in enumerate(zip(points,points[1:])):
            pattern=line(a,b,int(i==len(points)-2),clip,pattern)
        last=1
    else:
        last=int(case['last'])
        for kind,data in shaft_ops(points,case['width'],(338,169,372)[res],372,(0,0)):
            if kind=='line':
                line(data[:2],data[2:],last,bounds) # native joins always clip
            else:
                last=0
                if invalid or (clip and (max(y for x,y in data)<clip[1] or min(y for x,y in data)>clip[3])):continue
                for y,left,right in spans(data,clip):
                    if 0<=y<height:
                        for x in range(max(0,left),min(width-1,right)+1):paint(x,y,1)
                for i,(a,b) in enumerate(zip(data,data[1:]+data[:1])):
                    line(a,b,int(i==len(data)-1),clip)
                last=1
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


header=['/* Generated by src/tests/igs/generate_round_box_vectors.py; see round-box-vectors.json provenance. */',
        'static const struct round_box_case round_box_cases[] = {']
application=[]
for i,case in enumerate(cases):
    hashes=[];lasts=[]
    for phase in range(2):
        words,last=oracle(case,phase);hashes.append(digest(words));lasts.append(last)
    assert lasts[0]==lasts[1]
    res=case['resolution']
    if case['app']:
        words,last=oracle(case,2);app=dict(case)
        for key in ('clipping','bounds','last','app'):app.pop(key)
        app.update(last_line=last,runs=runs(words,res));application.append(app)
    values=[res,case['mode'],case['style'],case['width'],*case['caps'],int(case['clipping']),int(case['last']),lasts[0]]
    bounds=case['bounds'] or (0,0,319 if res==0 else 639,399 if res==2 else 199)
    header.append('\t{'+','.join(map(str,values))+',{'+','.join(map(str,case['points']))+'},{'+','.join(map(str,bounds))+'},{UINT64_C(0x%016x),UINT64_C(0x%016x)}},'%tuple(hashes))
    if i%250==0:print(f'{i}/{len(cases)}',flush=True)
header.append('};')
models=['verify_tos104_gdp.py','verify_tos104_wide.py','verify_tos104_polygon.py','verify_tos104_line_clip.py','verify_tos104_lines.py','verify_tos104_curves.py']
provenance=dict(generator='src/tests/igs/generate_round_box_vectors.py',references=['15-graphics-commands.adoc','43-native-line-clipping.adoc','44-native-software-lines.adoc','49-native-gdp-rounded-boxes.adoc','50-native-wide-lines.adoc','134-delivery-rectangles.adoc'],
    method='Independent native rounded geometry and circle/offset operations, rational spans, endpoint visits and per-plane Boolean equations; no SyncTERM output or Atari execution.',
    model_sha256={n:hashlib.sha256((reference/'tools'/n).read_bytes()).hexdigest() for n in models},
    rom_sha256=hashlib.sha256((reference/'TOS/tos104.img').read_bytes()).hexdigest(),native_cases=len(cases),positive_cases='1344 flag1 cases from the independently generated igs_box_vectors.h',
    native_initial='primary0x5aa5 XOR plane*0x1357, alternate0xa55a XOR plane*0x2468',native_foreground='Effective logical line pen10 retained across modes, physical9 masked to active depth; native custom mask0xa531',
    application_foreground='Wire C1,10 selects physical9 in low mode and logical1/active mask in medium/mono. Recolour zero-background runs to that independent pen result; XOR uses active mask. Custom type7 retains default0xffff.',
    text_after='Marked overlay cases render the console glyph after graphics; remove runs covered by its cell, then compose the independent glyph raster.',
    safe_domain='Discard host/status writes after native traversal; widened safe geometry, no raw memory/word-wrap emulation.')
root=Path(__file__).resolve().parents[2]
(root/'terminal/cterm/tests/data/igs_round_box_vectors.h').write_text('\n'.join(header)+'\n')
encoded=json.dumps(dict(provenance=provenance,cases=application),indent=2)
encoded=re.sub(r'\[\s*(-?\d+(?:,\s*-?\d+)*)\s*\]',lambda m:'['+', '.join(re.findall(r'-?\d+',m.group(1)))+']',encoded)
(Path(__file__).parent/'round-box-vectors.json').write_text(encoded+'\n')
print(f'Wrote {len(cases)} outline frames and {len(application)} application cases.',flush=True)
