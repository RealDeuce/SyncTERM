# SyncTERM 1.10 SkyPix report review

Reference: [SpitfireTN/syncterm-skypix report](https://github.com/SpitfireTN/syncterm-skypix/blob/647972fb9789deb5942f471b51a43be85578eafe/docs/report.md),
commit `647972fb9789deb5942f471b51a43be85578eafe`. The report targets
SyncTERM 1.10i, Synchronet commit `61a343a641`. This review concerns the modern
CMake tree; the old patch is not applied to its separate renderer.

| Reported issue | Current implementation and verification |
| --- | --- |
| SkyPix depends on RIPv3 and sends RIP identification | Separate Graphics values and parser ownership. `syncterm_skypix_report` selects SkyPix directly and verifies that a RIP version query produces no RIP response before the ANSI status reply. |
| Font command 10 loses text after its first bang; command 16 has the same framing risk | Bounded parser retains both string terminators. `cterm_skypix` exercises every input split, including named font selection. The application report test verifies positioned glyphs after font selection; existing transfer/cache suites exercise command 16 through actual XMODEM. |
| Fonts depend on the working directory and are absent from a normal install | Embedded native catalog supplies the 34 native reported strikes plus native Pearl/8, without installation. Exact installed strikes beside the INI remain optional overrides. `amiga_font_report` verifies every strike in both Workbench profiles. |
| Text batching and deferred command dispatch reorder drawing | Commands synchronize pending text before changing positions or drawing. `cterm_skypix` replays the synthetic report sequence at every byte split and byte by byte. `syncterm_skypix_report` checks the same independent scene through the application, including reset/replay. |
| Font reset leaves stale rendering state and breaks editor controls | Command `10;0` restores the configured default and its normal text routing. Both report regressions select Diamond, reset, then check literal Pearl A/B/D pixels after `ABC`, BS, EL0 and `D`. |
| Command 3 lacks flood fill | The native pixel surface implements synchronous seed-colour fill. Both report regressions immediately fill a just-drawn outline and check the complete scene in both planes. The interior is 44,451 pixels, intentionally larger than the old patch's quarter-screen guard; valid enclosed fills must not be rejected for their area. |

The application regression runs both Kick variants, CP437/UTF-8 and status on/off.
An ANSI reply fences a single capture of pixels, palette and application state.
Physical audio is disabled. Expected pixels come from literal rectangle bounds
and pinned Pearl/8 glyph rows, not from the emulator under test. These synthetic
sequences establish ordering/fill behaviour, not reproduction of the original
ACIS artwork or its network timing.

On 2026-11-08 the user supplied a current ACIS supporter-screen screenshot and
confirmed it looked correct. All four outlined letters have clean interior fills,
and the Amiga, Commodore, IBM and Skypix labels are separate. This is a manual
visual check, not an exact raster reference. The supplied repository has
screenshots but no raw ACIS capture.

## Font inputs

The built-ins embed native Amiga disk-font files unchanged and load them through
`st_amiga_font_decode()`, the same memory parser used for installed fonts. No
YAFF or SPF fonts participate. Kickstart 1.3 selects the 30 Workbench 1.3
strikes; Kickstart 3.1 selects the 30 Workbench 2.x+ strikes. All 30 stock native
files supplied in the report match the later set byte for byte. The reporter's
four additional native AWeb strikes (Courier and Times at 30 and 36 pixels)
are included in both profiles. Native Pearl/8 preserves the default.

Each profile exposes 35 native strikes. The supplied Topaz/8 and Sapphire/15
and /18 occur only in SPF and are excluded; Pearl/8 comes from the actual
native file in the local Amiga font drawer. The unsupported converted
Topaz/9 entry is removed. The
[native input manifest and provenance](../src/media/amiga_font/data/README.md)
record all 65 original files and image identities. The verified 2.04 Fonts
disk (`AmigaFonts2.0`) shares its stock fonts with the sampled 2.1/3.1 disks;
those bytes cannot identify which release supplied the reporter's copy.

The previous converted catalog and JR-Comm metric overrides have been removed.
`amiga_font_report` verifies independent native width, baseline, full byte-glyph
ink and advance hashes for every strike in both profiles. Native fonts are
not synthesized from another format to fill gaps in the supplied collection.

## Malformed Emberwood artwork

The user's `/tmp/rof.pcap` independently contains this public artwork fragment
in both sessions, frames 157 and 444:

```text
ESC[15;11!6;50;19;4!ESC[13;7;51;18;3!
```

`6;50;19;4!` lacks an escape/CSI prefix on the wire and therefore correctly
appears as printable text. It is a server-artwork defect, not discarded parser
framing. No terminal-specific repair is introduced.
