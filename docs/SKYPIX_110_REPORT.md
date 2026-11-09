# SyncTERM 1.10 SkyPix report review

Reference: [SpitfireTN/syncterm-skypix report](https://github.com/SpitfireTN/syncterm-skypix/blob/647972fb9789deb5942f471b51a43be85578eafe/docs/report.md),
commit `647972fb9789deb5942f471b51a43be85578eafe`. The report targets
SyncTERM 1.10i, Synchronet commit `61a343a641`. This review concerns the modern
CMake tree; the old patch is not applied to its separate renderer.

| Reported issue | Current implementation and verification |
| --- | --- |
| SkyPix depends on RIPv3 and sends RIP identification | Separate Graphics values and parser ownership. `syncterm_skypix_report` selects SkyPix directly and verifies that a RIP version query produces no RIP response before the ANSI status reply. |
| Font command 10 loses text after its first bang; command 16 has the same framing risk | Bounded parser retains both string terminators. `cterm_skypix` exercises every input split, including named font selection. The application report test verifies positioned glyphs after font selection; existing transfer/cache suites exercise command 16 through actual XMODEM. |
| Fonts depend on the working directory and are absent from a normal install | Compiled font catalog supplies all 38 requested family/size pairs without installation. Exact installed strikes beside the INI remain optional overrides. `amiga_font_report` verifies coverage and independently decoded native metrics/ink for matching strikes. |
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

## Font decisions

The supplied native and SPF collections together request 38 distinct family/size
pairs. The built-in catalog already contains all of them, plus Topaz/9:

| Family | Requested heights |
| --- | --- |
| Courier | 11, 13, 15, 18, 24, 30, 36 |
| Diamond | 12, 20 |
| Emerald | 17, 20 |
| Garnet | 9, 16 |
| Helvetica | 9, 11, 13, 15, 18, 24 |
| Opal | 9, 12 |
| Pearl | 8 |
| Ruby | 8, 12, 15 |
| Sapphire | 14, 15, 18, 19 |
| Times | 11, 13, 15, 18, 24, 30, 36 |
| Topaz | 8, 11 |

Twenty supplied native strikes match the selected built-in glyphs, advances and
baseline exactly. Their native nominal widths are now pinned in the manifest;
18 required a correction from the converted YAFF bounding/cell width. This
affects nominal-grid operations such as BS and tabs, not the measured advance
used for proportional wrapping. No glyph bitmap or individual advance changed.

The remaining supplied variants are not substituted for existing defaults, per
the user's choice. In particular the AWeb Courier/Times versions and the
Ruby/Diamond native metadata differ from the selected Workbench/JR-Comm versions.
The existing JR-Comm Ruby/Diamond baseline and width decisions remain in force.
The [native Workbench reference comparison](../src/media/amiga_font/reference/README.adoc)
subsequently established that the supplied 30 stock native strikes are exact
Workbench 2.1/3.1 files. The compiled Ruby/Diamond metric overrides equal the
1.3 values, and the five stock Courier conversions differ in width, advances
and some glyph placement; the YAFF `workbench-3.1_` filenames do not establish
equivalence to the native disk files. The reference sets do not change the
selected compiled fonts.
The reference inventory pins every supplied file's SHA-256 and marks which
entries assert exact equivalence versus family/size coverage only.

## Malformed Emberwood artwork

The user's `/tmp/rof.pcap` independently contains this public artwork fragment
in both sessions, frames 157 and 444:

```text
ESC[15;11!6;50;19;4!ESC[13;7;51;18;3!
```

`6;50;19;4!` lacks an escape/CSI prefix on the wire and therefore correctly
appears as printable text. It is a server-artwork defect, not discarded parser
framing. No terminal-specific repair is introduced.
