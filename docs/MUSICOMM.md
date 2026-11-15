# Musicomm

Choose **Start Musicomm (any key stops)** in the connected session's online
menu when the BBS asks you to activate its Musicomm accessory. SyncTERM waits
for the sender's start byte; nothing is recognized automatically in ordinary
terminal text. Any local key aborts, including while waiting for start.

SyncTERM uses an available output enabled by `MidiModes` first. If none opens,
it renders the accessory's Atari ST PSG preset through the regular audio
runtime and channel-0 music volume. `AudioModes=0` disables physical speaker
output; `MidiModes=0` disables MIDI independently. Console Quiet does not disable
this explicitly requested music. Starting Musicomm replaces existing music.
Ending, aborting, resetting, disconnecting or destroying the session stops its
sound. The feature works independently of the selected text/graphics emulation
and wire encoding. The stream must reach SyncTERM without text conversions or
software flow control; ordinary Telnet decoding still belongs to the transport.

MIDI playback selects bank 0, **Lead 1 (square)** on channel 1 (program 80,
or instrument 81 in one-based GM numbering) at the start of each session.
This is SyncTERM's approximation of the speaker preset; the original accessory
sends no instrument selection. It replaces any previously selected MIDI voice,
including the sawtooth used by ANSI music calibration.

The serial protocol uses hexadecimal byte values:

| State | Byte | Action |
| --- | --- | --- |
| Waiting | `02` | Reply `02`, then discard queued input until the receive queue is empty. |
| Waiting | Anything else | Discard. |
| Playing | `00`–`7F` | Toggle this MIDI note's independent on/off flag. |
| Playing | `80`–`FF` | Reply `FF FF FF`, silence sound and discard queued input until empty. |

A local abort also replies `FF FF FF`, even before a handshake. Reset/EOF and
session destruction cancel locally without sending a new completion reply.
Start and end drains include the suffix of a buffered write and subsequent
writes already queued. Buffer boundaries and periodic parser polls do not end
the drain: the session must observe an empty receive queue.

Every note event emits `90 note 40` for on or `90 note 00` for off on MIDI
channel 1. Completion sends `90 note 00` for all 128 notes before releasing the
MIDI destination. Note 2 has no special meaning after the handshake. NUL,
CR, LF, XON, XOFF and ESC are ordinary note numbers. There are no durations,
timestamps, song-file commands, tempo fields or per-note acknowledgments.
The sender must pace the toggles in real time. It must wait for the start echo
and leave an idle interval before notes, and leave another idle interval after
the end reply before resuming terminal traffic. The protocol defines no guard
interval length or second ready acknowledgment.

The speaker fallback reuses SyncTERM's independently tested GIST/PSG engine.
It uses Musicomm's 56-word preset, automatic three-voice allocation, volume 14,
priority 1 and a 200 Hz software envelope. Pitch lookup folds into notes 24–108;
logical note flags retain all 128 pitches. Voice replacement retains the old
note's channel association, so releasing that older note can release its
replacement. This intentionally preserves the receiver's behavior.

The supplied `PROTOCOL.txt` describes automatic release after approximately
one second. Further inspection of the supplied executable contradicts that
claim: `$0996` stores the nonnegative note pitch in the voice control word,
and `$0EAA`–`$0EAE` skips duration decrement when that word is nonnegative.
Thus Musicomm's held notes do **not** automatically consume the preset's
200-tick duration. Explicit release sets control to -1. SyncTERM preserves this
behavior; a native test holds notes for two seconds and checks it directly.

The source of the protocol and preset is Michael Ferrara's `MUSICOMM.ACC`,
10,860 bytes, supplied with `MUSICOMM.DOC` and the locally recovered
`PROTOCOL.txt` in `~/MusicComm`. The executable SHA-256 is
`1222a5a01114ab28c3e3034577de2afba3dd748364ead06163714a28960b62f0`.
Addresses above are relative to its loaded text segment; file offsets add
`0x1C`. Preset initialization is `$012C`–`$03B2`; note processing is
`$03B4`–`$0498`; handshake, drains and cleanup are `$058A`–`$06E6`;
PSG allocation and pitch lookup are `$08D2`–`$0AB6`. The original binary and
song files are not bundled. No original FoReM sender, song-file format,
physical MIDI device or Atari hardware playback was tested.

`cterm_musicomm` checks exact serial/MIDI bytes, every note-input split,
control/high-byte boundaries, drains, held pitches, stolen voices, PSG PCM,
abort/reset/EOF/destruction and runtime shutdown with recording/pull outputs.
`syncterm_musicomm` exercises real local-menu activation, handshake, all 128
notes, abort, disconnect and return to text using coherent bitmap captures in
CP437 and UTF-8, with ANSI and Atari ST/IGS sessions. Physical audio and MIDI
are disabled in those application fixtures.
