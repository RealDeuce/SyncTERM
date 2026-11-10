"""Independent maintainer oracle shared by circular/elliptical arc fixtures.

Expected geometry, arrows, rational spans and Boolean pixel visits come from
reference oracles; no SyncTERM code or captured pixels enter this model.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import sys


class NativeArcs:
    def __init__(self,reference):
        sys.path.insert(0,str(reference/'tools'))
        from verify_tos104_curves import vertices,arithmetic
        from verify_tos104_arrows import effect
        from verify_tos104_wide import shaft_ops
        from verify_tos104_polygon import spans
        from verify_tos104_line_clip import projected
        from verify_tos104_lines import locations,bit_rule
        self.vertices=vertices;self.arithmetic=arithmetic;self.effect=effect;self.shaft_ops=shaft_ops
        self.spans=spans;self.projected=projected;self.locations=locations;self.bit_rule=bit_rule

    def oracle(self,case,phase):
        vertices=self.vertices;arithmetic=self.arithmetic;effect=self.effect;shaft_ops=self.shaft_ops
        spans=self.spans;projected=self.projected;locations=self.locations;bit_rule=self.bit_rule
        res=case['resolution'];planes=(4,2,1)[res];width=320 if res==0 else 640;height=400 if res==2 else 200
        xs=(338,169,372)[res];xr=case['radius'];yr=case['radius_y'] if case.get('gdp',2)==6 else arithmetic(xr,xs,372)['result'];x,y=case['center']
        clip=case['bounds'] or (0,0,width-1,height-1);limits=clip if case['clipping'] else None
        mode=case['mode']-1
        words=[0 if phase==2 else (0x5aa5^((i%planes)*0x1357)) if phase==0 else (0xa55a^((i%planes)*0x2468)) for i in range(16000)]
        if limits and (x+xr<limits[0] or x-xr>limits[2] or y+yr<limits[1] or y-yr>limits[3]):return words,int(case['last'])
        begin,end=case['angles']
        if case['corrected']:
            turn=(min(begin,end)//360)*360;begin-=turn;end-=turn
        points,steps=vertices(begin*10,end*10,xr,yr,x,y,case.get('gdp',2))
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
