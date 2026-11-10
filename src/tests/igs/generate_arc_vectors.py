#!/usr/bin/env python3
"""Maintainer-only K geometry/line/cap fixtures, independent of SyncTERM output.

Usage: python3 src/tests/igs/generate_arc_vectors.py /path/to/IGS
Ordinary tests need only the checked-in frames, never a ROM or Atari runtime.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import hashlib
import itertools
import json
import re
import sys
from pathlib import Path
from filled_curve_oracle import digest, runs

reference=Path(sys.argv[1]).resolve()
sys.path.insert(0,str(reference/'tools'))
from verify_tos104_curves import vertices,arithmetic
from verify_tos104_arrows import effect
from verify_tos104_wide import shaft_ops
from verify_tos104_polygon import spans
from verify_tos104_line_clip import projected
from verify_tos104_lines import locations,bit_rule

cases=[]
def add(res,mode,style,width,caps,radius=17,angles=(25,275),center=(80,80),clip=True,last=True,app=False,text_after=False,corrected=False):
    cases.append(dict(resolution=res,mode=mode,style=style,width=width,caps=caps,radius=radius,angles=angles,center=center,
        clipping=clip is not False,bounds=None if isinstance(clip,bool) else clip,last=last,app=app,text_after=text_after,corrected=corrected))
for res,mode,style,begin,end in itertools.product(range(3),range(1,5),range(1,8),range(3),range(3)):
    add(res,mode,style,1,(begin,end),last=bool(style%2),app=style in (1,3,7) and (begin,end)==(1,2))
for res,mode,width,style,begin,end in itertools.product(range(3),range(1,5),(3,9,39),(1,3,7),range(3),range(3)):
    add(res,mode,style,width,(begin,end),radius=80 if width==39 else 17,center=(160,100) if width==39 else (80,80),last=False,
        app=width in (3,9) and style==1 and (begin,end)==(1,1))
for res,width in itertools.product(range(3),range(3,40,2)):
    add(res,1+width%4,1,width,(1,2),radius=70,last=False)
angles=((0,0),(0,1),(1,0),(0,45),(45,90),(90,180),(180,270),(270,360),(0,360),(360,0),
        (360,360),(359,1),(1,359),(25,275),(275,25),(0,720),(360,720),(720,720),(720,721),(720,0))
for res,mode,width,caps,arc in itertools.product(range(3),range(1,5),(1,9),((0,0),(1,2)),angles):
    add(res,mode,3,width,caps,angles=arc,last=False)
for res,mode,width,radius in itertools.product(range(3),range(1,5),(1,9),(0,1,2,3,4,7,31,127,128,131,132,511,512,515,516,-1,-3,-17)):
    add(res,mode,7,width,(1,1),radius=radius,last=False)
for res,mode,width,clip in itertools.product(range(3),range(1,5),(1,9),
        ((70,65,90,85),(120,30,140,100),(60,150,100,170),(90,85,70,65),(0,95,319,95),False)):
    add(res,mode,3,width,(1,1),clip=clip,angles=(270,90),last=True)
for res,steps in itertools.product(range(3),range(32,129)):
    add(res,3,3,1,(0,0),radius=4*steps,last=False)
for res,mode,width,center in itertools.product(range(3),range(1,5),(1,9),((3,4),(310,190))):
    add(res,mode,3,width,(1,1),center=center,clip=False,last=False)
for res,mode,width in itertools.product(range(3),range(1,5),(1,9)):
    add(res,mode,3 if width==1 else 1,width,(1,1),center=(100,18*(16 if res==2 else 8)+5),app=True,text_after=True)
# Corrected angle cases preserve the source's sweep and interpolation. A common
# whole-turn shift puts the independent native oracle in its safe positive
# table domain; it does not use an emulator-produced scene as the expectation.
for res,mode,width,arc in itertools.product(range(3),range(1,5),(1,9),
        ((-90,0),(-180,-270),(-361,-1),(-32768,-32761),(32760,32767),(3270,3360),(9999,9900),(9990,9999))):
    add(res,mode,3,width,(1,2),angles=arc,last=False,corrected=True)


def oracle(case,phase):
    res=case['resolution'];planes=(4,2,1)[res];width=320 if res==0 else 640;height=400 if res==2 else 200
    xs=(338,169,372)[res];xr=case['radius'];yr=arithmetic(xr,xs,372)['result'];x,y=case['center']
    clip=case['bounds'] or (0,0,width-1,height-1);limits=clip if case['clipping'] else None
    mode=case['mode']-1
    words=[0 if phase==2 else (0x5aa5^((i%planes)*0x1357)) if phase==0 else (0xa55a^((i%planes)*0x2468)) for i in range(16000)]
    if limits and (x+xr<limits[0] or x-xr>limits[2] or y+yr<limits[1] or y-yr>limits[3]):return words,int(case['last'])
    begin,end=case['angles']
    if case['corrected']:
        turn=(min(begin,end)//360)*360;begin-=turn;end-=turn
    points,steps=vertices(begin*10,end*10,xr,yr,x,y,2)
    triangles,modified=effect(points,case['width'],xs,372,case['caps'])
    def paint(px,py,pattern):
        if not 0<=px<width or not 0<=py<height:return
        for plane in range(planes):
            index=py*width*planes//16+(px//16)*planes+plane;mask=32768>>(px%16)
            value=bit_rule(mode,pattern,int(bool(words[index]&mask)),(9>>plane)&1)
            words[index]=(words[index]&~mask)|(mask if value else 0)
    def line(a,b,ending,bounds,pattern=65535):
        if bounds:
            if bounds[0]>bounds[2] or bounds[1]>bounds[3]:return pattern
            accepted,endpoints,_=projected((*a,*b),bounds)
            if not accepted:return pattern
        else:endpoints=(*a,*b)
        pixels=locations(endpoints,mode,ending);horizontal=endpoints[1]==endpoints[3]
        for i,(px,py) in enumerate(pixels):paint(px,py,(pattern>>(15-((px if horizontal else i)&15)))&1)
        count=len(pixels)%16
        return ((pattern<<count)|(pattern>>(16-count)))&65535 if count else pattern
    last=int(case['last'])
    ops=[('polygon',triangle) for triangle in triangles]
    if case['width']==1:
        pattern=(65535,65520,0xc0c0,0xff18,0xff00,0xf191,65535 if phase==2 else 0xa531)[case['style']-1]
        for i,(a,b) in enumerate(zip(points,points[1:])):
            pattern=line(a,b,int(i==len(points)-2),limits,pattern)
        last=1
    else:ops+=shaft_ops(modified,case['width'],xs,372,case['caps'])
    for kind,data in ops:
        if kind=='line':line(data[:2],data[2:],last,clip)
        else:
            last=0
            if limits and (limits[0]>limits[2] or limits[1]>limits[3] or max(py for px,py in data)<limits[1] or min(py for px,py in data)>limits[3]):continue
            for sy,left,right in spans(data,limits):
                if 0<=sy<height:
                    for px in range(max(0,left),min(width-1,right)+1):paint(px,sy,1)
            for i,(a,b) in enumerate(zip(data,data[1:]+data[:1])):line(a,b,int(i==len(data)-1),limits)
            last=1
    return words,last


header=['/* Generated by src/tests/igs/generate_arc_vectors.py; see arc-vectors.json provenance. */','static const struct arc_case arc_cases[] = {']
application=[]
for number,case in enumerate(cases):
    hashes=[];lasts=[]
    for phase in range(2):
        words,last=oracle(case,phase);hashes.append(digest(words));lasts.append(last)
    assert lasts[0]==lasts[1]
    if case['app']:
        words,last=oracle(case,2);app=dict(case);app['last_line']=last;app['runs']=runs(words,case['resolution']);application.append(app)
    values=[case['resolution'],case['mode'],case['style'],case['width'],*case['caps'],int(case['clipping']),int(case['last']),lasts[0],*case['center'],case['radius'],*case['angles']]
    bounds=case['bounds'] or (0,0,319 if case['resolution']==0 else 639,399 if case['resolution']==2 else 199)
    header.append('\t{'+','.join(map(str,values))+',{'+','.join(map(str,bounds))+'},{UINT64_C(0x%016x),UINT64_C(0x%016x)}},'%tuple(hashes))
    if number%250==0:print(f'{number}/{len(cases)}',flush=True)
header.append('};')
models=['verify_tos104_curves.py','verify_tos104_arrows.py','verify_tos104_wide.py','verify_tos104_polygon.py','verify_tos104_lines.py','verify_tos104_line_clip.py']
provenance=dict(generator='src/tests/igs/generate_arc_vectors.py',references=['15-graphics-commands.adoc','43-native-line-clipping.adoc','44-native-software-lines.adoc','46-native-polygon-fill.adoc','48-native-curve-helpers.adoc','49-native-gdp-rounded-boxes.adoc','50-native-wide-lines.adoc','51-native-arrowheads.adoc','138-delivery-curve-callers.adoc'],
    method='Independent native curve vertices, geometric arrows/circle offsets, rational polygon spans and ordered per-plane Boolean line visits; no SyncTERM output or Atari execution.',
    model_sha256={name:hashlib.sha256((reference/'tools'/name).read_bytes()).hexdigest() for name in models},rom_sha256=hashlib.sha256((reference/'TOS/tos104.img').read_bytes()).hexdigest(),
    native_cases=len(cases),corrected_cases=sum(c['corrected'] for c in cases),native_initial='primary0x5aa5 XOR plane*0x1357, alternate0xa55a XOR plane*0x2468',
    native_foreground='Logical line pen10 retained across modes, physical9 masked to active depth; custom mask0xa531; app custom mask0xffff',
    application_foreground='Wire C1,10 chooses physical9 in low mode and active mask in medium/mono; recolour zero-background nonzero runs, XOR always active mask.',
    correction='Widen degree multiplication/angle arithmetic and normalize only trig lookup. Corrected fixtures use a common whole-turn shift to derive native positive-domain expected geometry with the identical sweep; no pre-table ROM reads or signed-word angle overflow.',
    host_bounds='Preserve native coarse signed rejection and clip phase; discard host/status writes after traversal and keep the serial128-point limit separate from129 generated vertices.')
root=Path(__file__).resolve().parents[2]
(root/'terminal/cterm/tests/data/igs_arc_vectors.h').write_text('\n'.join(header)+'\n')
encoded=json.dumps(dict(provenance=provenance,cases=application),indent=2)
encoded=re.sub(r'\[\s*(-?\d+(?:,\s*-?\d+)*)\s*\]',lambda m:'['+', '.join(re.findall(r'-?\d+',m.group(1)))+']',encoded)
(Path(__file__).parent/'arc-vectors.json').write_text(encoded+'\n')
print(f'Wrote {len(cases)} native frames and {len(application)} application cases.',flush=True)
