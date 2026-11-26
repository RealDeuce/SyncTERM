Native Amiga disk-font files embedded unchanged.

The built-in catalog contains the 30 Workbench 1.3 strikes and the 30
Workbench 2.x+ strikes. SkyPix Kickstart 1.3 selects the first set; SkyPix
Kickstart 3.1 selects the second. The reporter's native Courier/30, /36 and
Times/30, /36 are additional strikes available in both profiles. Native
Pearl/8 from the local Amiga font drawer supplies the existing default in
both profiles. Each profile therefore exposes 35 strikes.

`manifest.json` pins every original file with its SHA-256, family, height,
profile membership and source path. `provenance.json` retains disk image
identities and the qualifications on archive labels. The verified 2.04 disk
and sampled 2.1/3.1 disks have identical stock font bytes. All 30 reporter
stock strikes match those later native files exactly; only its four extra
native strikes need separate copies. The original font data retains its
Commodore/Agfa or other original provenance, separately from project code.

`generate_fonts.py` only emits C byte arrays containing the exact native file
contents. It does not parse or convert fonts. At runtime the existing
`st_amiga_font_decode()` memory decoder loads those embedded bytes, exactly
as it loads a disk font. The same glyph representation and drawing path
serve both. The decoded built-ins are immutable, process-owned objects,
published with `st_call_once`; callers borrow them and release is a no-op.
No file access is required to decode a built-in. Installed native packages
remain explicit overrides and continue to use filesystem access.

The catalog contains no YAFF, SPF or converted replacement fonts. Sources
that do not supply native Amiga files are excluded. In particular, there are
no invented Sapphire/15 or /18 or Topaz/8 or /9 entries from other formats.
The older IcyTerm inputs, metric overrides and format-specific comparison
artifacts have been removed.

`native-reference.json` independently pins all 65 decoded inputs. Its
FNV-1a-64 digest starts at 14695981039346656037; each integer is a big-endian
16-bit word, with signed coordinates represented modulo 65536. Words are:
font height, baseline, nominal width; then, for each byte 0..255, advance,
number of lit pixels, and each lit pixel's X/Y sorted by Y then X. X is the
native bearing plus glyph column; Y is row+height-baseline-1. Missing bytes
select the native default glyph. This reference was decoded with amitools
0.8.1, independently of SyncTERM. CTest checks every strike in both profiles.

Workbench 1.3 versus Workbench 2.x+
-----------------------------------

The same 30 family/height pairs occur in both sets. All original file bytes
are retained. Printable ASCII glyph shapes agree in 29 strikes; Topaz/11
redraws 64 of the 95 printable ASCII glyphs. Other changes include control
and default glyphs and some byte-0xA0 mappings. Baselines change in 28
strikes and nominal widths in 24. These fields control positioning and
grid operations; they are independent of proportional glyph advances.

For these native files, byte offset `0x72` holds the big-endian 16-bit
nominal width and `0x74` holds the baseline. For example, Ruby/8 has
`00 0f` / `00 06` in 1.3 and `00 0b` / `00 05` in 2.x+. The runtime decoder
reads those bytes directly. It does not patch them.

| Strike | 1.3 width / baseline | 2.x+ width / baseline |
| --- | --- | --- |
| courier/11 | 6 / 9 | 6 / 8 |
| courier/13 | 7 / 10 | 7 / 9 |
| courier/15 | 9 / 12 | 9 / 11 |
| courier/18 | 11 / 14 | 11 / 13 |
| courier/24 | 15 / 19 | 15 / 18 |
| diamond/12 | 15 / 9 | 8 / 8 |
| diamond/20 | 20 / 14 | 12 / 13 |
| emerald/17 | 20 / 13 | 13 / 12 |
| emerald/20 | 20 / 15 | 13 / 14 |
| garnet/9 | 20 / 7 | 10 / 7 |
| garnet/16 | 20 / 11 | 10 / 10 |
| helvetica/9 | 2 / 7 | 5 / 6 |
| helvetica/11 | 3 / 9 | 6 / 8 |
| helvetica/13 | 4 / 10 | 7 / 9 |
| helvetica/15 | 4 / 12 | 8 / 11 |
| helvetica/18 | 5 / 14 | 10 / 13 |
| helvetica/24 | 6 / 19 | 14 / 18 |
| opal/9 | 12 / 7 | 10 / 6 |
| opal/12 | 12 / 9 | 9 / 8 |
| ruby/8 | 15 / 6 | 11 / 5 |
| ruby/12 | 15 / 9 | 11 / 8 |
| ruby/15 | 20 / 11 | 14 / 10 |
| sapphire/14 | 19 / 11 | 11 / 10 |
| sapphire/19 | 19 / 15 | 12 / 14 |
| times/11 | 2 / 8 | 5 / 7 |
| times/13 | 3 / 10 | 6 / 9 |
| times/15 | 3 / 11 | 7 / 10 |
| times/18 | 4 / 14 | 10 / 13 |
| times/24 | 6 / 18 | 13 / 17 |
| topaz/11 | 8 / 8 | 8 / 8 |
