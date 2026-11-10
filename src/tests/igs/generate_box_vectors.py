#!/usr/bin/env python3
"""Maintainer-only B fixtures: independent geometry, rational spans and bit rules.

Usage: python3 src/tests/igs/generate_box_vectors.py /path/to/IGS
No SyncTERM output or Atari execution constructs expected pixels.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import hashlib
import itertools
import json
import re
import sys
from pathlib import Path

reference = Path(sys.argv[1]).resolve()
sys.path.insert(0,str(reference/'tools'))
from verify_tos104_gdp import rounded
from verify_tos104_polygon import spans
from verify_tos104_line_clip import projected
from verify_tos104_lines import locations,bit_rule

source=(reference/'docs/igs/pattern-acceptance-data.adoc').read_text()
tables=json.JSONDecoder().raw_decode(source[source.index('{'):])[0]
user=[0,65535,0xa531,0x8001,0x5555,0xaaaa,0x0010,0x7ffe,
      0xf00f,0x00ff,0xff00,0x1357,0x2468,0x1111,0x8888,0x5aa5]
selections=[(0,1,[0]),(1,1,[65535])]
for interior,family,count,size,start in [(2,'DITHER',8,4,1),(2,'OEMPAT',16,8,9),(3,'HATCH0',6,8,1),(3,'HATCH1',6,16,7)]:
    rows=tables['tables']['_'+family]['words']
    selections += [(interior,start+i,rows[i*size:(i+1)*size]) for i in range(count)]
selections += [(4,1,user)]
cases=[]
for resolution,mode,selection,flag,perimeter in itertools.product(range(3),range(1,5),range(39),range(2),range(2)):
    order=(mode+selection)%4;x1,y1,x2,y2=33,32,93,74
    points=(x2 if order&1 else x1,y2 if order&2 else y1,x1 if order&1 else x2,y1 if order&2 else y2)
    cases.append(dict(resolution=resolution,mode=mode,selection=selection,flag=flag,perimeter=perimeter,
        points=points,clipping=True,bounds=None,last=bool((mode+flag)%2),
        app=selections[selection][:2] in [(1,1),(3,12)]))
for resolution,mode,flag,perimeter,(w,h) in itertools.product(range(3),range(1,5),range(2),range(2),
        [(0,0),(1,1),(2,2),(3,3),(7,2),(15,1),(1,15),(18,18),(60,0),(0,42),(12,7)]):
    cases.append(dict(resolution=resolution,mode=mode,selection=38,flag=flag,perimeter=perimeter,
        points=(33,32,33+w,32+h),clipping=True,bounds=None,last=False,app=False))
for resolution,mode,flag,perimeter,bounds in itertools.product(range(3),range(1,5),range(2),range(2),
        [(40,36,88,70),(40,100,88,110),(100,36,110,70),(80,70,40,36)]):
    cases.append(dict(resolution=resolution,mode=mode,selection=38,flag=flag,perimeter=perimeter,
        points=(33,32,93,74),clipping=True,bounds=bounds,last=True,app=False))
for resolution,mode,flag,perimeter,points in itertools.product(range(3),range(1,5),range(2),range(2),
        [(-4,2,15,20),(310,190,325,205)]):
    cases.append(dict(resolution=resolution,mode=mode,selection=38,flag=flag,perimeter=perimeter,
        points=points,clipping=False,bounds=None,last=True,app=False))


def geometry(case):
    res=case['resolution'];p=case['points'];left,right=sorted((p[0],p[2]));top,bottom=sorted((p[1],p[3]))
    if case['flag']:
        return rounded(p,319 if res==0 else 639,(338,169,372)[res],372)[0]
    return [(left,top),(right,top),(right,bottom),(left,bottom)]


def oracle(case,phase):
    res=case['resolution'];planes=(4,2,1)[res];width=320 if res==0 else 640;height=400 if res==2 else 200
    mode=case['mode']-1;_,_,rows=selections[case['selection']]
    words=[0 if phase==2 else (0x5aa5^((i%planes)*0x1357)) if phase==0 else (0xa55a^((i%planes)*0x2468)) for i in range(16000)]
    bounds=case['bounds'] or (0,0,width-1,height-1);clip=bounds if case['clipping'] else None
    points=geometry(case);perimeter=bool(case['perimeter'])
    def paint(x,y,p):
        # Host safety scissors writes after native clipping/endpoint traversal;
        # they do not create a new nonfinal endpoint at the canvas boundary.
        if not 0<=x<width or not 0<=y<height:return
        for plane in range(planes):
            index=y*(width*planes//16)+(x//16)*planes+plane;mask=32768>>(x%16)
            old=int(bool(words[index]&mask));v=bit_rule(mode,p,old,(5>>plane)&1)
            words[index]=(words[index]&~mask)|(mask if v else 0)
    invalid=bool(clip and (clip[0]>clip[2] or clip[1]>clip[3]))
    rejected=bool(clip and (max(y for x,y in points)<clip[1] or min(y for x,y in points)>clip[3]))
    if case['flag']:
        scan=[] if invalid or rejected else spans(points,clip)
        for y,left,right in scan:
            if 0<=y<height:
                for x in range(max(0,left),min(width-1,right)+1):paint(x,y,(rows[y%len(rows)]>>(15-x%16))&1)
        last=False
        if invalid or rejected:perimeter=False
    else:
        left,top=points[0];right,bottom=points[2]
        if clip:left=max(left,clip[0]);top=max(top,clip[1]);right=min(right,clip[2]);bottom=min(bottom,clip[3])
        if not invalid:
            for y in range(max(0,top),min(height-1,bottom)+1):
                for x in range(max(0,left),min(width-1,right)+1):paint(x,y,(rows[y%len(rows)]>>(15-x%16))&1)
        last=case['last']
    if perimeter:
        edges=list(zip(points,points[1:]+points[:1]));last=True
        for i,(a,b) in enumerate(edges):
            if invalid:continue
            endpoints=(*a,*b)
            if clip:
                accepted,endpoints,_=projected(endpoints,clip)
                if not accepted:continue
            for x,y in locations(endpoints,mode,int(i==len(edges)-1)):paint(x,y,1)
    return words,int(last)


def digest(words):
    result=14695981039346656037
    for word in words:
        for byte in (word>>8,word&255):result=((result^byte)*1099511628211)&0xffffffffffffffff
    return result


def runs(words,res):
    planes=(4,2,1)[res];width=320 if res==0 else 640;height=400 if res==2 else 200;result=[]
    for y in range(height):
        row=[sum(((words[y*width*planes//16+(x//16)*planes+p]>>(15-x%16))&1)<<p for p in range(planes)) for x in range(width)]
        start=0
        while start<width:
            end=start+1
            while end<width and row[end]==row[start]:end+=1
            if row[start]:result.append((y,start,end-start,row[start]))
            start=end
    return result


header=['/* Generated by src/tests/igs/generate_box_vectors.py; see box-vectors.json provenance. */',
        'static const struct box_case box_cases[] = {']
application=[]
for number,case in enumerate(cases):
    hashes=[];lasts=[]
    for phase in range(2):
        words,last=oracle(case,phase);hashes.append(digest(words));lasts.append(last)
    assert lasts[0]==lasts[1]
    res=case['resolution'];interior,index,_=selections[case['selection']]
    if case['app']:
        app=dict(case);words,last=oracle(dict(case,last=True),2)
        for key in ('selection','bounds','clipping','last','app'):app.pop(key)
        app.update(interior=interior,index=index,last_line=last,runs=runs(words,res));application.append(app)
    values=[res,case['mode'],interior,index,case['flag'],case['perimeter'],int(case['clipping']),int(case['last']),lasts[0]]
    bounds=case['bounds'] or (0,0,319 if res==0 else 639,399 if res==2 else 199)
    header.append('\t{'+','.join(map(str,values))+',{'+','.join(map(str,case['points']))+'},{'+','.join(map(str,bounds))+'},{UINT64_C(0x%016x),UINT64_C(0x%016x)}},'%tuple(hashes))
    if number%250==0:print(f'{number}/{len(cases)}',flush=True)
header.append('};')
models=['verify_tos104_gdp.py','verify_tos104_polygon.py','verify_tos104_line_clip.py','verify_tos104_lines.py','verify_tos104_curves.py']
provenance=dict(generator='src/tests/igs/generate_box_vectors.py',references=['15-graphics-commands.adoc','46-native-polygon-fill.adoc','47-native-rectangles.adoc','48-native-curve-helpers.adoc','49-native-gdp-rounded-boxes.adoc','131-patterns-acceptance.adoc','134-delivery-rectangles.adoc'],
    method='Independent rounded point construction, rational polygon spans, clipped native endpoint visits and per-plane Boolean equations; no SyncTERM output or Atari instruction execution.',
    model_sha256={name:hashlib.sha256((reference/'tools'/name).read_bytes()).hexdigest() for name in models},rom_sha256=tables['rom_sha256'],native_cases=len(cases),
    native_foreground='Physical5 masked to active depth; effective logical pen7 retained across mode changes',
    native_initial='primary0x5aa5 XOR plane*0x1357, alternate0xa55a XOR plane*0x2468',native_hash='FNV-1a-64 over all32000 big-endian native bytes, each phase independently',
    application_foreground='Wire C2,7 selects physical5 in low resolution and falls back to logical1/active mask in medium/mono. Recolour zero-background run coverage to that independent pen result; XOR uses active mask.',
    safe_domain='Wide signed coordinates discard outside-canvas writes after native segment traversal; no adjacent-memory writes, wrapped radius/delta arithmetic or fault emulation.')
root=Path(__file__).resolve().parents[2]
(root/'terminal/cterm/tests/data/igs_box_vectors.h').write_text('\n'.join(header)+'\n')
encoded=json.dumps(dict(provenance=provenance,user=user,cases=application),indent=2)
encoded=re.sub(r'\[\s*(-?\d+(?:,\s*-?\d+)*)\s*\]',lambda m:'['+', '.join(re.findall(r'-?\d+',m.group(1)))+']',encoded)
(Path(__file__).parent/'box-vectors.json').write_text(encoded+'\n')
print(f'Wrote {len(cases)} native and {len(application)} application cases.',flush=True)
