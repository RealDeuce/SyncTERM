"""Maintainer-only independent glyph-array and line-pixel expectations for W.

The supplied glyph oracle was checked separately against the original addressed
software text model. Adapt only its fixture foreground and safe host write gate;
font geometry/effects/clipping stay independent of SyncTERM code and output.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
from array import array
import inspect
import json
import sys
import zlib

SCREEN=0x40000

class PixelView:
    def __init__(self,words,width,height,foreground):
        self.words=words;self.host_width=width;self.host_height=height;self.foreground=foreground
    def backing(self,address):return self.words.get(address,0)

class NativeText:
    def __init__(self,reference):
        sys.path.insert(0,str(reference/'tools'))
        import oracle_delivery_text_effects as original
        from verify_tos104_line_clip import projected
        from verify_tos104_lines import locations,bit_rule
        self.fonts=json.loads((reference/'docs/igs/generated/tos104-fonts.json').read_text())
        self.projected=projected;self.locations=locations;self.bit_rule=bit_rule
        code=inspect.getsource(original.oracle)
        old='   for plane in range(b.backing(0x2adc)):'
        assert code.count(old)==1 and code.count('(5>>plane)&1')==1
        code=code.replace(old,'   if not (0<=px<b.host_width and 0<=py<b.host_height):continue\n'+old)
        code=code.replace('(5>>plane)&1','(b.foreground>>plane)&1')
        namespace=dict(original.__dict__);exec(code,namespace);self.glyph=namespace['oracle']

    def frame(self,c,phase,application=False):
        res=c['resolution'];planes=(4,2,1)[res];width=320 if res==0 else 640;height=400 if res==2 else 200
        words={SCREEN+2*i:(0 if application else (0x5aa5^((i%planes)*0x1357)) if phase==0 else (0xa55a^((i%planes)*0x2468))) for i in range(16000)}
        foreground=(5 if res==0 else (1<<planes)-1) if application else 5&((1<<planes)-1)
        def pixel(px,py,value):
            for p in range(planes):
                index=SCREEN+2*(py*width*planes//16+(px//16)*planes+p);mask=32768>>(px%16)
                words[index]=(words[index]|mask) if value&(1<<p) else (words[index]&~mask)
        def console_z():
            glyph=self.fonts[2 if res==2 else 1]['glyphs'][90];ch=16 if res==2 else 8
            for yy,row in enumerate(glyph):
                for xx,bit in enumerate(row):pixel(96+xx,18*ch+yy,((1<<planes)-1) if bit=='#' else 0)
        if application and not c.get('text_after',False):console_z()
        view=PixelView(words,width,height,foreground);b=view.words
        f=self.fonts[{8:0,9:1,10:2,16:0,18:1,20:2}[c['points']]];factor=2 if c['points']>=16 else 1;double=factor==2
        top,ascent,half=[f[k]*factor+int(double) for k in ('top','ascent','half')]
        bottom,descent,underline=[f[k]*factor for k in ('bottom','descent','ul_size')]
        style=c['style'];rotation=c['rotation'];angle=((rotation+1 if rotation<0 else 0 if rotation>4 else rotation)*900)%3600;mode=c['mode']-1;outline=int(bool(style&16))
        left,right=(f['left_offset']*factor,f['right_offset']*factor) if style&4 else (0,0)
        advance=f['max_cell_width']*factor+2*outline
        total=len(c['text'])*advance+left+right;align_h,align_v=c['align'];delh=(0,total//2-outline,total-2*outline)[align_h]
        delv=(top,top-half,top-ascent,top+bottom,top+descent,0)[align_v]
        delh+=(left,half*right//top,ascent*right//top,0,descent*left//bottom,left+right)[align_v]
        x,y=c['origin'];u=outline
        origin=((x-delh-u,y-delv-u),(x-delv-u,y+delh+u+1),(x+delh+u+1,y-(top+bottom-delv)-u),(x-(top+bottom-delv)-u,y-delh-u))[angle//900]
        x,y=origin;ulin=(-1 if double else 0) if bottom<=underline else 1
        start=((x,y+top+underline+ulin),(x+top+underline+ulin,y),(x,y+bottom-(underline+ulin)),(x+bottom-(underline+ulin),y))[angle//900]
        step=((0,1),(1,0),(0,-1),(-1,0))[angle//900]
        clip=c['bounds'] or (0,0,width-1,height-1)
        b.update({0x2b36:style,0x2b44:angle,0x2b42:int(double),0x2b1e:65535,0x2b20:1,0x2b1c:32767,
                  0x2b3c:f['thicken']*factor,0x2b22:1,0x2b40:left,0x2b3e:right,0x2b3a:f['skew'],0x2b38:f['lighten'],
                  0x2adc:planes,0x2ada:80 if res==2 else 160,0x2b00:mode,0x2b12:int(c['clipping'])})
        for a,v in zip((0x2b14,0x2b16,0x2b18,0x2b1a),clip):b[a]=v
        for character in c['text']:
            b[0x2b28]=x;b[0x2b2a]=y;b[0x2b24]=character*f['max_cell_width']
            rows=[''.join('1' if bit=='#' else '0' for bit in row) for row in f['glyphs'][character]]
            b.update(self.glyph(view,rows))
            if double:b[0x2b1c]-=f['max_cell_width']
            dx,dy=((advance,0),(0,-advance),(-advance,0),(0,advance))[angle//900];x+=dx;y+=dy
        if style&8 and c['text']:
            sx,sy=start;ex,ey=(x,sy) if angle%1800==0 else (sx,y);mask=f['lighten'] if style&2 else 65535
            for _ in range(underline):
                endpoints=(sx,sy,ex,ey);accepted=True
                if c['clipping']:
                    if clip[0]>clip[2] or clip[1]>clip[3]:accepted=False
                    else:
                        accepted,endpoints,_=self.projected(endpoints,clip);accepted=accepted==1
                if accepted:
                    positions=self.locations(endpoints,mode,int(c['last']))
                    for index,(px,py) in enumerate(positions):
                        if not (0<=px<width and 0<=py<height):continue
                        ink=(mask>>(15-(px%16 if endpoints[1]==endpoints[3] else index%16)))&1
                        for p in range(planes):
                            address=SCREEN+2*(py*width*planes//16+(px//16)*planes+p);bit=32768>>(px%16)
                            value=self.bit_rule(mode,ink,int(bool(b[address]&bit)),(foreground>>p)&1)
                            b[address]=(b[address]&~bit)|(bit if value else 0)
                    count=len(positions)%16
                    if count:mask=((mask<<count)|(mask>>(16-count)))&65535
                sx+=step[0];ex+=step[0];sy+=step[1];ey+=step[1]
                mask=(mask>>1)|(mask&32768)|((mask&1)<<15)
        if application and c.get('text_after',False):console_z()
        return [b[SCREEN+2*i]&65535 for i in range(16000)]

def checksum(words):
    data=array('H',words)
    if sys.byteorder=='little':data.byteswap()
    raw=data.tobytes()
    return (zlib.crc32(raw)<<32)|zlib.adler32(raw)
