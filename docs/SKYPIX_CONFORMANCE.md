# SkyPix compatibility

The Graphics selector provides **SkyPix (Kick 1.3)** and **SkyPix (Kick 3.1)**
independently of RIP. Both use the practical dialect selected in the
reconciliation table below. The variant selects the documented Kickstart
ellipse arithmetic and per-character proportional-text overhang rules. It
does not select a different wire protocol.

Selecting SkyPix chooses the 640x200, 4:3 canvas and Latin-1 as initial
preferences. Screen Mode, Encoding and Font remain editable and configure the
connection. Screen Mode selects pixel geometry/aspect and the system cell grid;
the Amiga strike supplies independent native text metrics. In 640x200 the UI
uses 80x25 cells of 8x8 pixels, including with Diamond 20. The optional
**Amiga 640x400** mode uses a 4:3 canvas and 80x50 system cells of 8x8 pixels.
Native Amiga strikes keep the same pixel metrics in either mode, with more
vertical space in the 400-line canvas. Each Amiga preset has its own descriptor
and saved mode identity, including the system font selector, cursor and palette
policy. The 200-line mode remains the initial SkyPix preference. Graphics and native
text are clipped to the drawable area within the selected mode. The optional
status row consumes the last system-cell row inside that mode, without changing
its resolution or aspect ratio. Amiga 640x200 therefore provides 192 drawable
lines with status and 200 without; Amiga 640x400 provides 392 or 400. SyncTERM's
8-pixel status bar intentionally differs from SkyTERM's 10-pixel top bar.
Ordinary Font preferences remain available outside SkyPix. Disconnected scrollback retains
connected geometry; returning to the directory restores its geometry.

Application UI, including status, online menu, file picker and transfer dialogs,
uses system CP437 rather than the connection's Amiga font. Exact system strikes
are used at the screen mode's supported cell sizes; unusual screen-mode cell
dimensions scale a system strike. Selecting an Amiga font does not change those
dimensions or scale application UI. Fixed-cell conversion preserves the entire
Amiga bitmap, including rows below the baseline. Pixel-positioned JR-Comm text retains
its separate native baseline convention. Classic bitmap fonts carry byte indexes,
including C1 glyph positions, without a charset tag; the selected codepage maps
bytes to glyphs, while UTF-8 text uses Latin-1 glyph mapping. Remote SkyPix palette
changes do not recolour application UI. SkyPix drawing and text write identically
into both blink phase planes at the pixels they paint. Pixels outside the operation,
including transparent JAM1 glyph backgrounds, retain their native indexed/RGB
values and separate phase contents.

## Raster implementation and checks

SkyPix palette indexes occupy bank 512–527. Its 16-entry alignment leaves the
low four index bits equal to the Amiga colour number. Points, lines, rectangle
fills, ellipse outlines/fills, brush blits, native text and erasure edit the
console's existing raster under its write lock. The console first resolves any
pending text and circular row offset, then marks the cells intersecting the
reported damage. A point uses the existing single-pixel console operation.
Drawing does not copy or republish the full canvas, and canvas bounds are
independent of the underlying raster stride.

Brush minterms operate on all enabled colour bits together with boolean
expressions, using the truth-table half for a set C input. Disabled planes retain
their destination bits. There is no indexed-to-RGB round trip; planar ILBM input
is decoded once when the brush is loaded. Capturing a brush copies only its
requested rectangle and retains the documented source Y+1 convention.

Cursor rendering saves its underline for exact restoration, then complements
the live native/RGB pixels in place. Text retains a clipped background for
Unicode cluster revision and paints directly into the raster; transparent JAM1
pixels and both restored phases are preserved. Erasure needs no temporary image.
Depth reduction and scrolling still save the raster across retained-cell updates
to preserve padding and both phases; these are full-canvas operations, rather
than the per-primitive snapshots removed here. Flood fill retains its bounded
visited map and queue, needed to prevent repeated visits in boundary-fill mode.
The parser, signed-word/ROM walkers, font metrics, ILBM validation and bounded
audio queue retain their existing algorithms.

`cterm_skypix` checks literal primitive damage on a 31x23 canvas in a 40x32
raster, with different indexed/RGB phase contents, clipping, rejected/empty
operations, brush capture, transparent glyphs and exact revision restoration,
in both variants and CP437/UTF-8. Its brush truth-table checks enumerate all
256 minterm bytes, all sixteen masks, depths 1–4 and eight/sixteen display colours
against an independent per-bit oracle. `st_console_bitmap` checks that edits
borrow the actual raster, resolve pending text and nonzero circular row offsets,
mark only affected cells, and publish reported damage even on callback errors.
The application conformance fixture adds literal corner/boundary point checks
in both phases using reply-fenced coherent captures.

## Reference provenance

The references are the user-supplied `SKYPIX_PROTOCOL.adoc`,
`SKYPIX_ALGORITHMS.adoc`, `SKYPIX_SOUND_FORMAT.adoc`, `SKYTERM_PROTOCOL.adoc` and
`SKYTERM_SCROLLING.adoc` in `~/SkyPix/docs/`,
with disassembly and ROM probe results in the same
research tree. They establish original-terminal behaviour; the reconciliation
table records intentional combined-dialect choices, with SkyTERM preferred when
resolving differences, including the palette and
depth rules from `ANSI.TXT`. JR-Comm executable SHA-256:
`8c2e140158531c9095b0439150c887af34d7c938f24045eb44f383d307db0fe6`.
SkyTERM executable SHA-256:
`cf7f3064e2b749a35c42640c7a23d5708c386afb51ab3be57f1817c438e0473b`.

The scrolling reference separates text-anchor limits from raster bounds.
SyncTERM's earlier `floor(drawable_height / font_height) * font_height`
raster height stranded twelve pixels during Times/15 scrolling with status
on a 200-line mode. Later eight-pixel scrolling moved that stranded artwork
back into view, producing gaps inside RoF's already-drawn border boxes.
The raster now spans the full drawable area, following SkyTERM's window
rectangle policy; the selected font still controls line advance and anchor
limits. The local status reservation and normal CR/LF policy remain as
described above and in the reconciliation table.

`src/terminal/cterm/tests/data/skypix/golden-vectors.json` preserves the supplied
independent line, ellipse and Diamond 20 text results (JSON SHA-256
`a7ba8d85c74ac60000cd104399413e88a2360fd5368224d7f63e998cd8fd638d`). Ordered point hashes use
signed big-endian 16-bit x/y pairs. No expectations are generated from SyncTERM.
The supplied research reports 28,684 ROM cases and five native codec cases;
the repository test runs the representative vectors retained in that JSON,
not the complete external research suite. Reference ROM SHA-256 identities:

| ROM | SHA-256 |
| --- | --- |
| Kickstart 1.3 | `ee05862d8102a08436ac4056da7d549db31625c7d47b24dfb7b3c9a5c113ca53` |
| Kickstart 3.1 | `6d43840d4099a74170ea0f0425b6257c3891ebcaa39c4d1840075a9ab22b5707` |
| Kickstart 2.04 A500+ | `d0b70e8a1772614b897f92c33cb299bed3fc8e3de488fc12f67f97fc2486eb79` |
| Kickstart 2.04 A3000 | `563f948af19c09daed1f06b8760221ff4b2789c1690cdcfbce2c941157af23d1` |

The latter two references agree with the 3.1 rendering rules in the retained
cases. The built-in font catalog embeds 65 unchanged native Amiga disk-font
files and parses them with the same memory decoder as installed fonts. The
30 Workbench 1.3 strikes belong to the Kickstart 1.3 profile, and the 30
Workbench 2.x+ strikes belong to the Kickstart 3.1 profile. Native Pearl/8
and the supplied native Courier/30, /36 and Times/30, /36 are shared, yielding
35 strikes per profile. Input SHA-256 identities, disk provenance and
independent native glyph/metric hashes are in `src/media/amiga_font/data/`.
No converted font inputs or metric overrides are used.

The 2026-11-08 RoF capture supplied two sessions with identical public welcome
artwork, with status enabled and disabled. The retained network-line cases
use each profile's native Times/15 metrics (`tf_XSize=3` for 1.3 and 7 for 2.x+).
The native strike's identity is pinned in the font manifest. Literal final X/Y
and complete glyph-mask hashes come from independently decoding that strike;
the capture supplies input only. The earlier first-line Kick 1.3 mask
`4ee6aa59a25a1e0b3eea6cdd9052f85cf3ba93225ce5bcd9d8f8be7b7c29e7e4`
was checked by issuing Text separately for each byte through the original
1.3 ROM using the later font. Selecting the actual Workbench 1.3 Times/15
(SHA-256 `85366fd141dc455976d3ced219089b3786c7ef3e4e203a22cbf3b85a7092629e`)
moves its native baseline from 10 to 11. Independent decoding with the same
per-byte clipping rule gives the current first-line mask
`fd904ee3a54e917843de0460d8118deb23b3801bbb8341498c412dd8a0bb3b6f`.
The same 3.1 ROM replay retains the prior mask; both finish at X=488.
The second line now uses its captured pen 11 with the captured sixteen-colour
request, giving steel blue rather than the former eight-colour alias to red.
Neither the full session nor login data is bundled. JR-Comm 1.02a instruction probes also issue `ScrollRaster` between
the artwork's bottom line, ellipses and copyright text as intervening CR/LF
controls reach the current font's bottom margin. Those effects are preserved;
they are not evidence that the ellipse walker itself differs from the ROM.
The later SkyTERM instruction replay also scrolls these components on its
200-line canvas. The 400-line canvas avoids that overflow in both originals;
this does not establish the artwork's intended resolution. No copyright
expectation is inferred from either result.

The 2026-11-05 rendering update also supplies 41 position-probe checks,
45 cursor-probe checks and 12 ROM cursor placement checks under `~/SkyPix/disassembly/`, driven by the original
JR-Comm instructions. They distinguish nominal BS/HT width from proportional
advance, normal CR/LF, font-height anchors, ANSI's eight-pixel positioning and
receive-buffer-dependent wrapping. These recorded-call probes do not rasterize
full-screen scrolling or establish physical sprite/timer behaviour.

Cursor shape, complement colours and command enable rules now follow the
preferred SkyTERM reference instead of those historical JR-Comm sprite rules.
One deliberate exception is the user's clean-cursor policy: restore the actual
visible cursor's background before output, movement or capture, rather than
reproducing SkyTERM's abandoned underlines or phase artifacts. The native enable
flag is tracked separately from the live cursor's saved bitmap strip.
`SKYTERM_PROTOCOL.adoc`'s cursor section pins the primitive's exact nominal
width, baseline and absence of an idle blink timer. The supplied `skyterm/rom-probe-results.json`
records original SkyTERM-derived 8x8, 9x12 and 20x20 rectangles and paired
Kickstart 1.3/3.1 RectFill results, including second-toggle restoration.
No executable or ROM bytes are bundled. SyncTERM retains its selected text
anchors, zero-origin UI layout, normal CR/LF and ANSI movement semantics;
cursor rectangles use those anchors rather than SkyTERM's ten-pixel title bar.

## Reconciliation decisions (2026-11-08)

The selected target is one practical dialect shared by both Kickstart variants,
with choices made feature by feature from the JR-Comm and SkyTERM references:

| Area | Selected contract |
| --- | --- |
| Text | Wrap by actual mapped-glyph advance before painting, independent of packet boundaries and explicit flushes. Preserve separate ANSI CR/LF, nominal BS/tabs, zero-origin coordinates, configured default font and safe fallback. |
| Kickstart rendering | Per-character text: Kick 1.3 clips each glyph's final overhang without recovering it on later characters; Kick 3.1 retains it. Preserve the verified ellipse arithmetic variants. |
| Depth | SyncTERM starts and fully resets in sixteen colours. Command 17 mode 1 selects eight colours; mode 2 selects sixteen. Preserve geometry and contents, reset palette/pens to foreground 2/background 0, mask existing SkyPix pixels on reduction and never restore discarded bits on expansion. Invalid modes do nothing. |
| Palette | Use the sixteen RGB4 defaults in ANSI.TXT and SkyTERM. Command 11 retains all sixteen supplied entries at either depth. Command 12 resets palette/pens without changing depth. |
| Scrolling | Preserve normal scrolling. The raster rectangle spans the complete drawable area, including pixels below the last whole font row, following SkyTERM's ScrollRaster bounds. Font metrics determine line advance and the text anchor's bottom limit, not the pixel rectangle. The earlier proposal to disable scrolling at command 17 was withdrawn because neither original terminal establishes that behaviour. JAM1 is only a drawing mode. Command 99 enables scrolling idempotently and changes no font, palette or position; it has no disabling argument. |
| Brushes | Standard ByteRun1, including 0x80 no-op and valid 128-byte runs. Retain bounded decoding and CMAP-before-BODY validation. Captured brushes inherit active depth/palette; received brushes own their CMAP. Apply supplied colours once on first blit, preserve unspecified colours and promote display depth when needed. Failed replacement preserves the old brush. |
| Delays | Command 14 uses 50 Hz ticks; zero is a no-op, and one/two ticks are honoured. Pauses retain unread bytes, keep local UI responsive, and cancel on reset/EOF. Command 99 idempotently enables already-enabled scrolling and changes no font, palette or position; it cannot disable scrolling. |
| Cursor | SkyTERM bitmap complement underline, exact current nominal width at the text baseline, no timed blinking. Any SkyPix command clears cursor operations at its first bang; default-font selection and SGR 0 re-enable them. The visible cursor has separate saved background so movement and output never abandon old underlines. Cursor removal precedes brush capture and scrolling; current bitmap snapshots retain the visible cursor. |
| Retained interfaces | Keyboard/modifiers, raw controller-1 mouse reports, bounded gadget definition/fill and ANSI replies. No gadget execution, hit-test feedback, controller-2 reports or drawing acknowledgments. |
| Transfers and primitives | Type-3 samples load on receipt/cache hit and command 9 plays on channel 1; type 2 remains cache-only. Preserve cache-response bytes, CRC XMODEM, duplicate-block protection, confined paths, cancellation/error handling, native mode-1 flood, outline-plus-boundary-fill ellipses and Y+1 brush capture. |

IFF/8SVX decoding, analog-filter emulation, local gadget execution, Chat Buffer, modem and AREXX functionality
remain excluded. Existing configuration and Kickstart selection values remain
stable. RoF remains the normal application test; copyright-specific regression
expectations are deferred until that investigation is resolved. The separate
Amiga 640x400 preset is implemented without changing the 200-line default.

The 2026-11-10 RoF Full Menu capture sends `ESC[10;8;!Pearl.font!` and
`ESC[10;9;!Opal.font!`. The trailing empty numeric field does not move the
font name out of string position zero. Original-instruction probes ran 27
cases each through JR-Comm 1.02a, the pinned SkyTERM 1.2b, C-Net 5.01's local
display, Skyline 1.2, SkyPlay 1.1 and SkyPaint 2.0. All six consume these
names and accept explicit extra numeric fields too. Height-zero font reset
completes at the first bang without collecting a name. These probes use
successful synthetic font opens and recording graphics APIs; they establish
parsing and call arguments, not a full Amiga session or raster comparison.

SyncTERM exposes numeric parameters and string arguments as separate lists.
The implemented string commands, 10 and 16, each have one string argument;
their handlers accept additional numeric fields after the required prefix.
Storage is fixed: at most 17 numeric fields including the command, 32 decimal
digits per field, and one string of at most 1,024 bytes. Accepted numbers keep
16-bit wrapping. An oversized recognized string command is discarded through
its closing bang without dispatch or name leakage; EOF clears unfinished
collection. ESC retains the existing interrupted-command recovery. Font-family
and transfer-path validation still apply after wire parsing.

## Supported behaviour

| Area | Policy and independent checks |
| --- | --- |
| Parsing | Independent numeric and string positions, with at most 17 numbers, 32 digits per number and one 1,024-byte string. Font/transfer framing is independent of extra numeric fields; height-zero font reset has no string. Every input split, bytewise delivery, exact/over-limit cases, unterminated collection, EOF/recovery, opaque escape strings and 16-bit wrapping have native checks. SkyPix and RIP select separate parsers. |
| Lines and ellipses | Directed line ties and signed-word arithmetic; Kick 1.3 32-bit versus Kick 3.1 64-bit ellipse recurrences; ordered ROM point vectors. Work is bounded for hostile overflow cycles outside the checked reference domain. |
| Pens and fills | RGB4 palette, initial sixteen colours, point/line/rectangle/ellipse/flood operations and brush minterms. Command 3 uses flood mode 1 regardless of its mode argument; command 13 outlines then performs boundary flood. Command 17 selects eight/sixteen colours and restores the documented palette/pens without altering scrolling. Depth reduction masks existing native pixel/cell indices in both phases, preserving status and padding; expansion does not recover discarded bits. Command 11 retains all sixteen entries at either depth. |
| Font position controls | Native default home uses its baseline, retaining the complete strike. Command 19 uses its logical pixel anchor. Native-font CR retains Y; LF adds current height; BS subtracts nominal width without a zero clamp; HT uses eight nominal widths; VT is inert. ANSI horizontal movement preserves native pixel Y, and vertical movement preserves pixel X; full cursor placement sets both axes. The RoF Ruby/15-to-Pearl/8 caller header retains its Y=36 strike across CSI 7 C, preventing the next line from erasing the first columns. Native tests cover movement aliases and every input split; application tests use literal Pearl glyph rows in both variants/encodings/status choices at 640x200/640x400. Diamond/20 position-probe regressions and literal Ruby/12 accent rows remain independent expectations. |
| Native-font wrapping | Actual mapped-glyph advance is measured before drawing. Diamond/20 fits 33 i characters on its first line, wraps the 161st, and wraps 32 W characters from X=100 to finish at X=100 on the next line. Network splits and explicit flushes give identical results. Wrap-off uses the nominal last-column origin. Comparison uses widened arithmetic before storing signed-word coordinates. Unicode revisions replace the previous advance without wrapping or retiring history twice. |
| Native-font scrolling | With full-screen margins, LF advances by the current height and clamps at the last nominal baseline, using current ascent/descent. At the bottom Diamond/20 scrolls exactly 20 pixels and its Text baseline stays at Y=194 on a 640x200 canvas without status (Y=174 with status). An off-grid overshoot scrolls the exact excess. The entire 200/192-pixel drawable area moves, including the font-row remainder: Ruby/12's last eight pixels and Diamond/20's last twelve pixels with status are not stationary padding. Cell-aligned moves retain cell text; fractional moves retain raster data without stale cell text. Status and mode padding outside the drawable area are preserved. Native two-phase stripe tests and reply-fenced application captures cover both variants/encodings/status choices. Reference: JR-Comm `H17:0ba6–0bfa`, `H17:2264–2408` for text-anchor limits; SkyTERM `H00:239c–23b6` and `SKYTERM_SCROLLING.adoc` for the raster extent. |
| Native-font erasure | ANSI EL0/1/2 use raw pixel X and the current font's ascent/descent, preserving position. The independent reference is `H16:11a0–11ca`, `H17:1c08–1dce` and geometry at `H17:0ba6–0bd0`: EL0 clears X to the canvas edge; EL1 clears through X+nominal-width-1; EL2 clears the whole strike row. Native tests check literal rectangle boundaries and both pixel planes; application tests check tcsh's BS CSI K sequence with Diamond/20. JAM2 spaces paint the full font-height background even when the compiled empty glyph has a one-row mask; named-font JAM1 spaces preserve pixels. Full proportional JAM2 horizontal-extent equivalence to Amiga ROMs remains unclaimed. |
| Cursor | Bitmap complement underline, one scanline at the Text baseline (`anchor+H-1`), X unchanged and nominal W exactly. Active native plane bits are XORed in both blink planes; the palette controls the result. Negative/off-canvas portions clip without moving the logical anchor. No overlay or idle timer. The live underline owns its exact saved background independently of C; it is removed before glyph output/revision, movement, erasure, scrolling, reset, brush capture and disconnect. This intentionally fixes SkyTERM's stale-position artifacts, including save/restore and horizontal movement. Unknown/rejected SkyPix commands remove the actual cursor and clear C at the first bang, before any string payload. `10;0` enables C without unconditionally drawing; named fonts clear it. SGR 0 enables C and restores the configured default font. Text draws at its new X when X<=width-9; executed ANSI/control movements and erasures redraw at the resulting position if enabled. Queries and save-position do not disturb it. The selected ANSI geometry and separate CR/LF remain. The existing nominal-width BS policy is retained; cursor removal does not erase the underlying glyph. DECTCEM hides/restores the live cursor as an extension. Current UI/CustomCursor snapshots retain its bitmap pixels; brushes and retired history do not acquire abandoned underlines. |
| Fonts and text | Configured initial/reset Amiga strike (Pearl 8 by default); exact installed strike, exact compiled strike, then configured default for unavailable remote requests. Initial unavailable preferences fall back to Pearl 8; no strike stretching. Native baseline, signed kerning/spacing, monospaced centring and proportional advances. Font selection, including fallback, uses JAM1; reset uses JAM2. Every byte is rendered separately: Kick 1.3 clips that glyph's trailing overhang permanently; Kick 3.1 retains it. |
| Mouse | Left-button-down sends the literal `ESC[21;1;x;y!` with native zero-based canvas pixels and no newline; independent of gadgets or host enable commands. Releases, other buttons, motion, status/padding and UI-handled events do not report. Local Alt/Command-O and the online menu gate forwarding; the normal status bar shows bright/dim M. Disabled events retain button synchronization; wheel-up opens local scrollback. Both variants/encodings/status choices and drawable scaling have independent application tests. |
| Gadget definitions | Command `22;n;c;x1;y1;x2;y2!` retains five 16-bit words in slot 1–20 and paints the inclusive rectangle with the current foreground pen using command 4's clipping rules. Replacing a record paints the new rectangle without erasing old pixels. Invalid slots or parameter counts have no record/drawing effect. Font/palette/display clears retain records; full terminal reset clears them. Native tests cover all slots, every input split, numeric wrapping and clipping; application tests check both pixel planes and raw mouse reports inside/outside the rectangle. No acknowledgment, button decoration or stored-command execution is added. Reference: supplied protocol's portable-implementation section and JR-Comm `H16:2564–25da`. |
| Brush capture | Command 6 captures starting at Y+1 with the active depth and palette. Copy uses source plane depth and does not clear unrelated destination planes. Received and captured brush palettes apply once on first blit; a deeper brush promotes the display to sixteen colours. Supplied palette entries replace current entries; unspecified entries remain intact. Invalid replacement retains prior pixels, depth, palette and pending state. |
| ILBM | Raw and standard ByteRun1, including 0x80 no-op and 128-byte literal/repeat runs. CMAP must precede BODY and is retained with the brush, quantized to RGB4 using the upper nibble of each component. Dimensions come from BMHD, not the transfer request. One to four planes and a four-million-pixel decoded limit. Mask planes are unsupported. |
| Delays | Command 14 uses 50 Hz ticks; zero is a no-op, and one/two ticks are honoured. Pauses retain unread bytes, keep local UI responsive, and cancel on reset/EOF. Command 99 idempotently enables already-enabled scrolling and changes no font, palette or position; it cannot disable scrolling. |
| Sound | Type 3 has a six-byte big-endian header (uint32 PCM count, uint16 rate), followed by signed 8-bit mono PCM. Positive lengths up to 4 MiB and nonzero rates are accepted; odd lengths are valid and trailing padding is ignored. Command 9 ignores speed/start/end and uses its loop count; zero deliberately plays once rather than waiting indefinitely as the originals do. A bounded incremental linear converter uses the native 16-bit period `(3579545 / rate) & 65535`, preserves phase across blocks/repetitions and feeds shared channel 1 at 44100-Hz stereo with existing gain/pan. Remote parsing waits through playback and backend latency; local UI remains responsive. Connected, menu and picker input waits service the owner at bounded intervals without parsing remote input. Disabled/unavailable/failed output releases the hold. Finish/EOF cancels playback; reset/destruction unloads samples. No physical Paula waveform, analog filter or device-chunk-gap emulation is claimed. |
| Transfers | Command 16 accepts brush mode 1, IFF sound mode 2, FutureSound mode 3 and general-file mode 20 using shared native CRC XMODEM workers with independent SkyPix state. All types share the active BBS's `skypix` directory, matching SkyTERM's BRU: destination; JR-Comm instead uses its configured download directory for mode 20. Modes 2/3/20 preserve cached bytes, including XMODEM padding, without brush replacement. Type 3 also loads signed mono PCM after cache publication or on a cache hit; a failed load leaves the file cached and retains the old sound. Type 2 has no IFF sound decoder. The second filename bang is the exact transfer boundary; no RIP CRLF skipping. Cache hits send ten CAN, ten backspaces and CR. Only completed files publish, after ILBM validation for mode 1. Failure/cancellation discards partial files and preserves the previous brush. Other modes remain declined. |
| Font manager | Menu-only picker installs complete `.font` families beneath `fonts/amiga/` beside the active INI. All referenced strikes validate before replacement. Removal cannot remove compiled fonts. Selected fonts own their loaded data and survive package replacement/removal. Metadata queries expose errors without eagerly copying unused Wren values. |

IFF sound decoding and colour/outline disk fonts remain unsupported.
Raw mouse reports do not depend on gadget definitions. Controller-2 menu reports
and local execution of stored gadget commands remain unestablished in the supplied
JR-Comm analysis and are not implemented.
Unsupported transfer modes are declined with the ordinary cancellation response.
This implementation deliberately bounds
malformed arguments rather than reproducing JR-Comm stale-parameter reads or
memory corruption.

## Remaining text compatibility gaps

The updated JR-Comm research describes behaviour beyond this checked subset:
ANSI movement in eight-pixel steps with non-eight-pixel fonts and mapping
custom ANSI scrolling margins into the native pixel model. ANSI positioning
still uses the screen-mode cell grid on the moved axis; the other axis retains
its native pixel offset. The ordinary
cell path for a fixed, grid-aligned default font retains cell-based wrapping;
the native-font path uses measured advances. Glyphs are painted
per character; tests establish packet/flush independence, not an Amiga serial
arrival schedule. JR-Comm's former forecast/count wrapping is intentionally
superseded by the measured-advance policy. Cursor rendering uses SkyTERM's
bitmap operations, not JR-Comm's sprite or 185-ms timer. SyncTERM does not adopt
SkyTERM's unrelated ANSI parser quirks or its fixed ten-pixel title origin.
Exact VBLANK/task scheduling remains outside these tests. Local UI selects
its ordinary cell cursor and snapshots restore terminal bitmap contents.

Graphics history uses the shared pixel-strip path. SkyPix retires RGB strips
at the exact scroll distance, merging any unfinished strip on the next scroll.
A 20-pixel move produces two eight-pixel rows and keeps four pixels pending;
the next 20-pixel move completes three rows. Both phases and colours are frozen
when pixels leave the canvas. SkyPix also captures RGB alongside aligned cell
rows so a later font change can join them without rerendering old text with a
different font or palette. Other emulations retain their optional text-only
history path.

History reads include the pending pixels within the configured row capacity,
aligned at the live boundary; any padding is confined to the oldest row.
Queries do not flush the pending strip or rotate the physical ring. Reset
retains it with the rest of history; destruction releases it. Native tests
cover palette changes, both phases, ring wrap, zero/one-row capacity, reset,
detached snapshots and subsequent ordinary cell scrolling. Application tests
check live, disconnected and directory history before and after a merge.
Pixel-only text retains its raster without invented character-cell content.

## Test inventory

Enable `ST_CONSOLE_TEST`, configure/build with CMake, then run:

```sh
ctest --test-dir <build> -R 'amiga_font|cterm_skypix|skypix_rom_vectors|syncterm_skypix|syncterm_amiga_font_manager' --output-on-failure
```

| Test | Coverage |
| --- | --- |
| `amiga_font` | HUNK relocation, zero-offset references, metrics, bit rasters, malformed input, descriptors and confined paths; compiled catalog, package lifecycle and retained loaded fonts. |
| `amiga_font_report` | All 65 native inputs, with exact independent metrics and full byte-glyph ink/advance hashes for all 35 strikes in each Kickstart profile. Non-native font formats are excluded. |
| `font_catalog` | Literal Pearl g/p/y rows retain descenders in fixed console cells. |
| `amiga_font_transactions` | Injected rename/rollback failures and secondary cleanup failures before/after the package publication boundary. |
| `cterm_skypix_audio` | Independent native raw vector, literal pre-mixer linear PCM, signed-byte alphabet, quantized periods, varying pull/block sizes and repeat phase; odd/max/truncated/zero/oversized data, allocation/open/backend failures, latency, bounded long-sample playback, channel-1 routing, zero-as-one, parser split/holds, previous-sample retention, reset/EOF/shutdown and active destruction. Pull/fake backends only. |
| `cterm_skypix` | Parser splits, independent numeric/string positions, exact and overflowing field/string bounds, unterminated-input and EOF recovery, command/state/pixel expectations, both variants, status geometry, resets, delays, proportional overhang, standard ByteRun1 boundaries/no-op, brush palette/depth ownership and malformed replacement, palette/depth transitions, short delays, and literal Diamond text positions. Raster/position fixtures explicitly gate cursor operations rather than depending on overlay exclusion. |
| `cterm_skypix_cursor` | Independent SkyTERM shape/colour expectations and clean-cursor lifecycle: nominal widths 8/9/12/20, text baseline, exact two-phase background restoration, clipping, first-bang entry, unknown/rejected commands, named/default fonts, SGR reset, no idle phase, native 8/16-colour indices, every input split and UTF-8 revision. Save/restore, brush capture and retired scrollback reject abandoned cursor pixels. Both Kick variants and status choices. |
| `syncterm_skypix_cursor` | Reply-fenced complete-scene expectations from literal Pearl A/B/é rows and cursor rectangles. Both variants, CP437/UTF-8, status choices and 640x200/640x400; idle stability, command/default/named-font transitions, three/four-plane palette-dependent colour and menu snapshot restoration, with no console cursor overlay. |
| `skypix_rom_vectors` | Independent ordered point sets/hashes and Diamond 20 text for the retained ROM vectors. |
| `syncterm_skypix_conformance` | Real application over loopback TCP, both variants with status on/off, fixed mode resolution/aspect/system grid, status within the selected mode, Current/Custom/non-Amiga geometry and padding, and Latin-1/CP437/UTF-8; literal Pearl A/g/p/y rows, synthetic C1 glyphs, Ruby 12 metrics, editor save/reload and system-font UI isolation; both variants with Diamond 20 in CP437/UTF-8 on Amiga 640x200/640x400, proportional cursor movement and unscaled 8x8 menu rasters; literal native glyph rows below Y=199 and exact mode/status clipping; palette/brush pixels, transfers/cache/error recovery, responsive delays, reset/EOF and disconnected scrollback. Replies fence one coherent capture of related cells, pixels, palette and state. |
| `syncterm_skypix_welcome` | RoF's captured Times/15 network lines remain at their pixel anchors; independent native-strike hashes check every glyph pixel in both planes, palette and cursor, both variants/encodings/status choices, split input and reset/replay. |
| `syncterm_skypix_report` | Separate SkyPix selection without RIP identification; immediate outline/fill above quarter-screen area, positioned text, two-bang font selection, font reset and BS/EL0 editing. Extra numeric fields select literal Pearl/8 and Opal/9 glyphs, retain right-edge advances and preserve height-zero reset on 640x200/640x400. Literal complete-scene pixels in both planes, palette/state, reset/replay and both variants/encodings/status choices. Native `cterm_skypix` adds every input split and bytewise replay. See the [1.10 report review](SKYPIX_110_REPORT.md) for issue mapping, font decisions and manual ACIS provenance. |
| `syncterm_skypix_cache` | Independent filename arguments with extra numeric fields on uploads/cache hits. Types 2/3 upload opaque bytes with LF/NUL/high bytes and padding; SOH/STX, bad-CRC retry, duplicate protection, shared-namespace cache hits across types 2/3/20, path rejection, unsupported modes, cancellation/EOF cleanup and terminal-input recovery. Both variants/encodings/status choices; one reply-fenced capture checks retained brush pixels in both planes and unchanged palette. Also covers successful and failed type-3 loads, cached sample replacement, odd lengths, type-2/20 isolation and reset. Physical audio remains disabled, so this fixture verifies muted recovery rather than audible playback. |
| `syncterm_skypix_mouse` | Exact documented click bytes, zero-origin/sub-cell precision and scaled drawable mapping; no report on releases/motion/other buttons/status/local UI; bright/dim M and both toggle routes, disabled-release recovery, local wheel scrollback and reset; both variants/encodings/status choices. |
| `syncterm_skypix_scrolling` | Literal four-pixel colour stripes through exact Diamond/20 scrolls, full drawable raster extent, stable bottom cursor, partial-strip joins, live/disconnected/detached viewers, both variants/encodings/status choices. Continuous box outlines cross the old font-row boundary on 640x200/640x400, then survive Times/15 to Ruby/8 to grid-aligned Pearl/8 scrolling. Each reply-fenced capture checks the complete drawable scene in both planes; expectations come from literal geometry and SkyTERM's ordinary ScrollRaster copy rule, not emulator captures or a RoF screenshot. |
| `syncterm_graphics_scrollback` | Shared ANSI/RIP/SkyPix image history, both phases, palette changes, composition/overlays, remote Pearl descenders, status choices and disconnected/offline viewing. |
| `syncterm_amiga_font_manager` | Real picker cancellation, install, replacement confirmation, stale snapshots, metadata, removal and malformed family rejection. |
| `wren_host` | A drained long-sample queue is refilled while connected and menu VMs wait for local input, with a hidden pixel cursor and preserved caller deadline; connected destruction unlinks the borrowed polling owner. |
| `bbs_list`, `wren_menu_contract` | All Graphics values, legacy RIP fallback/canonical save, locked merge/default persistence and conflicts, menu catalogs, constants and immutable family metadata. |

Headless fixtures disable physical audio with `AudioModes=0`. Bitmap and native
algorithm tests make no claim about physical Amiga hardware, audible sound or
native display backend behaviour. ANSI CP437/UTF-8 and historical RIP suites
remain separate regressions with their original reference provenance.

### Test execution cost

Pixel-only captures defer Python cell-object construction until cells or text
are accessed. Cell-section lengths are still checked immediately, and deferred
cells use owned bytes from the same coherent capture. Complete cursor/report/
scrolling scenes compare both planes as word bytes rather than constructing a
Python integer object for every observed pixel. Scrolling-box expectations
use background rows and literal borders; all 20 canvas/scroll combinations
were checked byte-for-byte against the former per-pixel geometry equations.
No scene, profile, source glyph/hash, input split, reply fence or cursor idle
interval was removed.

Native FreeBSD /usr/bin/cc Release configure/full builds passed without
warnings. All 11 SkyPix tests passed before the changes, and all 18 relevant
SkyPix/control/IGS-DEL regressions passed afterward. Literal protocol checks
also cover deferred-cell ownership, malformed cell-section lengths and
mismatch rejection in either pixel plane. The unchanged seven SkyPix
application suites' summed time fell from 182.48 to 148.55 seconds locally.
Cursor tests fell from 33.59 to 24.02 seconds, scrolling from 32.04 to 20.11,
and report tests from 15.31 to 11.79. These are local measurements, not CI
timing thresholds.
