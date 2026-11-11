#!/usr/bin/env python3
"""Maintainer-only original GIST data and independent software-voice fixtures.

Usage: python3 src/tests/igs/generate_gist_vectors.py /path/to/IGS
No SyncTERM output, CPU execution, PCM reference matching or foreign clocks.
"""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import hashlib
import json
from pathlib import Path
import random
import sys

reference=Path(sys.argv[1]).resolve();sys.path.insert(0,str(reference/'tools'))
from verify_gist_tick import oracle, WORDS, LONGS
from verify_gist_api import commands_oracle, apply_commands
root=Path(__file__).resolve().parents[2]
patch_file=reference/'docs/igs/generated/source-sound-patches.json'
gist_file=reference/'docs/igs/generated/gist.json'
patches={p['name']:p['words'] for p in json.loads(patch_file.read_text())}
ordered=[patches['a'+chr(97+i)] for i in range(19)]+[patches['landing']]
assert all(len(p)==56 for p in ordered) and ordered[3]==ordered[11]
source=next(g for g in json.loads(gist_file.read_text()) if g['path']=='IGS221/IG221.ACC')
header=['/* Original IGS 2.21 patch/pitch/factor words; see gist-vectors.json provenance. */',
        'const uint16_t st_igs_gist_patches[ST_IGS_GIST_PATCHES][ST_IGS_GIST_WORDS] = {']
for words in ordered:header.append('\t{'+','.join('0x%04x'%(w&65535) for w in words)+'},')
header+=['};','static const uint16_t gist_pitch[85] = {', '\t'+','.join(map(str,source['pitch_periods_notes_24_to_108'])),'};',
         'static const int16_t gist_factors[16] = {'+'\t'+','.join(map(str,source['volume_multipliers']))+'};']
(root/'terminal/igs/st_igs_gist_data.h').write_text('\n'.join(header)+'\n')


def unpack(words):
    r={off:words[off//2] for off in WORDS}
    r.update({off:words[off//2]*65536+words[off//2+1] for off in LONGS})
    return r


def pack(r):
    words=[0]*70
    for off in WORDS:words[off//2]=r[off]&65535
    for off in LONGS:words[off//2],words[off//2+1]=(r[off]>>16)&65535,r[off]&65535
    return words


def row(words):return '{'+','.join('0x%04x'%w for w in words)+'}'


rng=random.Random(0x49475347495354)
records=[]
for words in ordered:
    records.append([w&65535 for w in words]+[65535,1]+[0]*12)
for n in range(1024):
    words=[rng.randrange(65536) for _ in range(70)]
    words[0]=rng.choice((0,1,2,65535))
    words[3]=rng.randrange(16)
    for index in (4,18,40):words[index]=rng.choice((0,1,2,3,4,0x100,0x101,0x104,0xffff))
    for index in (13,29,51):
        value=rng.choice((0,1,65535,65536,0x80000000,0x7fffffff,rng.randrange(2**32)))
        words[index:index+2]=[value>>16,value&65535]
    for index in (17,39,55):words[index]=rng.choice((0,1,65535))
    records.append(words)
vectors=['/* Independent scalar-record equations; see gist-vectors.json provenance. */',
         'static const struct gist_case gist_cases[] = {']
for number,words in enumerate(records):
    voice=number%3;state=unpack(words)
    expected,writes=oracle(state,voice)
    assert len(writes)<=5
    vectors.append('\t{'+str(voice)+','+row(words)+','+row(pack(expected))+','+str(len(writes))+',{' +
                   (','.join('{%d,%d}'%w for w in writes) or '{0,0}')+'}},')
vectors.append('};')
vectors.append('static const uint16_t pitch_reference[85] = {'+','.join(map(str,source['pitch_periods_notes_24_to_108']))+'};')
voice_count=len(records)
api_cases=[]
for n in range(160):
    records=[[rng.randrange(65536) for _ in range(70)] for _ in range(3)]
    for record in records:
        record[0]=rng.choice((0,1,10));record[57]=rng.choice((0,1,7,65535))
    patch=[w&65535 for w in ordered[n%20]]
    if n%7==0:patch[0]=0
    request=rng.choice((0,1,2,3,65535,32768))
    volume=rng.choice((0,3,15,16,255,65535,32768))
    pitch=rng.choice((0,23,24,60,108,109,255,32767,65535))
    priority=rng.choice((0,1,7,65535))
    expected,result,commands,_=commands_oracle(records,patch,request,volume,pitch,priority)
    masks=(255,15,255,15,255,15,31,255,31,31,31,255,255,15,255,255)
    regs=[rng.randrange(256)&mask for mask in masks]
    after,writes=apply_commands(regs,commands)
    assert len(writes)<=6
    api_cases.append((records,patch,(request,volume,pitch,priority),expected,-1 if result==0xffffffff else result,regs,after,writes))
vectors.append('static const struct gist_on_case gist_on_cases[] = {')
for records,patch,args,expected,result,regs,after,writes in api_cases:
    vectors.append('\t{{'+','.join(row(r) for r in records)+'},'+row(patch)+','+row(args)+','+str(result)+',{' +
                   ','.join(row(r) for r in expected)+'},'+row(regs)+','+row(after)+','+str(len(writes))+',{' +
                   (','.join('{%d,%d}'%w for w in writes) or '{0,0}')+'}},')
vectors.append('};')
(root/'terminal/igs/tests/gist_vectors.h').write_text('\n'.join(vectors)+'\n')
provenance=dict(generator='src/tests/igs/generate_gist_vectors.py',
    references=['06-audio.adoc','56-gist-interrupt-voices.adoc','57-gist-caller-apis.adoc'],
    method='Original source patch/pitch/factor words plus the separate immutable-field integer oracle from verify_gist_tick.py. Explicit stable records, raw words and ordered register writes; no SyncTERM output, native instruction execution, CPU clocks or PCM matching.',
    source_sha256={str(f.relative_to(reference)):hashlib.sha256(f.read_bytes()).hexdigest() for f in (patch_file,gist_file,reference/'tools/verify_gist_tick.py',reference/'tools/verify_gist_api.py')},
    patches=20,words_per_patch=56,voice_cases=voice_count,api_cases=len(api_cases),random_seed='0x49475347495354',
    raw_volume_policy='Stored words retained; unsafe dynamic factor indices are bounded to0..15 at use. Random oracle cases use valid factor indices; dedicated literal tests cover the safe-port policy.')
(Path(__file__).parent/'gist-vectors.json').write_text(json.dumps(provenance,indent=2)+'\n')
print(f'Wrote 20 original patches,85 pitch periods,16 factors,{voice_count} voice cases and{len(api_cases)} API cases.')
