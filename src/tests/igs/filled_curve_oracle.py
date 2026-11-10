"""Independent maintainer oracle shared by native filled-circle/ellipse fixtures.

Uses source-derived reference geometry, rational spans, original pattern words
and whole-plane-word Boolean equations. It never imports SyncTERM code/output.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import json
import sys


class NativeFilledCurves:
    def __init__(self,reference):
        sys.path.insert(0,str(reference/'tools'))
        from verify_tos104_gdp import closed_curve
        from verify_tos104_curves import arithmetic
        from verify_tos104_polygon import spans
        from verify_tos104_line_clip import projected
        from verify_tos104_lines import locations,bit_rule
        self.closed_curve=closed_curve;self.arithmetic=arithmetic;self.spans=spans
        self.projected=projected;self.locations=locations;self.bit_rule=bit_rule
        table_source=(reference/'docs/igs/pattern-acceptance-data.adoc').read_text()
        tables=json.JSONDecoder().raw_decode(table_source[table_source.index('{'):])[0]
        user=[0,65535,0xa531,0x8001,0x5555,0xaaaa,0x0010,0x7ffe,0xf00f,0x00ff,0xff00,0x1357,0x2468,0x1111,0x8888,0x5aa5]
        selections=[(0,1,[0]),(1,1,[65535])]
        for interior,family,count,size,start in [(2,'DITHER',8,4,1),(2,'OEMPAT',16,8,9),(3,'HATCH0',6,8,1),(3,'HATCH1',6,16,7)]:
            rows=tables['tables']['_'+family]['words']
            selections += [(interior,start+i,rows[i*size:(i+1)*size]) for i in range(count)]
        selections += [(4,1,user)]
        self.tables=tables;self.user=user;self.selections=selections

    def oracle(self,case,phase):
        arithmetic=self.arithmetic;closed_curve=self.closed_curve;spans=self.spans
        projected=self.projected;locations=self.locations;bit_rule=self.bit_rule;selections=self.selections
        res=case['resolution'];planes=(4,2,1)[res];width=320 if res==0 else 640;height=400 if res==2 else 200
        x,y=case['center'];xr=case['radius'];yr=case['radius_y'] if case.get('gdp',4)==5 else arithmetic(xr,(338,169,372)[res],372)['result']
        mode=case['mode']-1;_,_,rows=selections[case['selection']]
        words=[0 if phase==2 else (0x5aa5^((i%planes)*0x1357)) if phase==0 else (0xa55a^((i%planes)*0x2468)) for i in range(16000)]
        bounds=case['bounds'] or (0,0,width-1,height-1);clip=bounds if case['clipping'] else None
        if clip and (x+xr<clip[0] or x-xr>clip[2] or y+yr<clip[1] or y-yr>clip[3]):return words,int(case['last'])
        points=closed_curve(case.get('gdp',4),xr,yr,x,y);last=0
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
