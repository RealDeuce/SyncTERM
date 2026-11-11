#!/usr/bin/env python3
"""Maintainer-only extraction of original patch words for b scene checks."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import hashlib
import json
from pathlib import Path

PIN = '76263e9fb923779160766fd3341c4ff4ebb13c9b58a322a71426cbff0d82ac93'


def generate(root):
    path = root/'docs/igs/generated/source-sound-patches.json'
    raw = path.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == PIN
    addressed = {}
    for row in json.loads(raw):
        for address in row['matching_text_relative_addresses']['221']:
            assert address not in addressed or addressed[address] == row['words']
            addressed[address] = row['words']
    rows = sorted(addressed.items())
    assert len(rows) == 20 and all(len(words) == 56 for _, words in rows)
    return dict(generator='src/tests/igs/generate_effect_vectors.py',
                references=['06-audio.adoc', '24-effect-patch-command.adoc',
                            '55-native-tos-sound-timer.adoc'],
                source_sha256={'docs/igs/generated/source-sound-patches.json': PIN},
                method='Original words in independent effect/address order, including separate identical ad/al rows. Big-endian hex, 56 words per row; no SyncTERM output.',
                patches_hex=[''.join(f'{word & 65535:04x}' for word in words) for _, words in rows],
                safe_port='Negative b20 word indices reject the command without store/play; native RAM/code/MMIO escapes are not reproduced. Saved records are copied once per session, restored from their current contents and reset only on full reset/reconnect.',
                audio='Local 44100Hz samples drive GIST at 200 updates/second and built-in TOS console service at 50/second, first service at 15ms. Disabled output advances control and PSG generator state on local monotonic time without PCM synthesis; old envelopes/tails do not replay. A dedicated continuous chip source shares GIST/BEL/click, ends at EOF/reset/disable and owns no worker/thread/foreign clock.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference', type=Path)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    output = Path(__file__).with_name('effect-vectors.json')
    text = json.dumps(generate(args.reference), indent=2)+'\n'
    if args.check:
        assert output.read_text() == text
    else:
        output.write_text(text)
