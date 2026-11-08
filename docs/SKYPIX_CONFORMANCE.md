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
changes do not recolour application UI. SkyPix drawing and text publish identically
into both blink phase planes.

## Reference provenance

The references are the user-supplied `SKYPIX_PROTOCOL.adoc`,
`SKYPIX_ALGORITHMS.adoc`, `SKYPIX_SOUND_FORMAT.adoc` and `SKYTERM_PROTOCOL.adoc` in `~/SkyPix/docs/`,
with disassembly and ROM probe results in the same
research tree. They establish original-terminal behaviour; the reconciliation
table records intentional combined-dialect choices, including the palette and
depth rules from `ANSI.TXT`. JR-Comm executable SHA-256:
`8c2e140158531c9095b0439150c887af34d7c938f24045eb44f383d307db0fe6`.
SkyTERM executable SHA-256:
`cf7f3064e2b749a35c42640c7a23d5708c386afb51ab3be57f1817c438e0473b`.

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
cases. No ROM or executable is bundled. Font inputs, original conversion
headers, upstream license, per-strike hashes and native Diamond/Ruby baseline
corrections are retained in `src/media/amiga_font/data/`. The 39 strikes in 11
families come from IcyTerm commit
`b568f6a087b7ececb28d0b45edd38f70a6bf692d` and compile into build-tree C data.

The 2026-11-08 RoF capture supplied two sessions with identical public welcome
artwork, with status enabled and disabled. The retained network-line cases
use Times/15's native `tf_XSize=7`, rather than its YAFF bounding width of 14.
The native strike's identity is pinned in the font manifest. Literal final X/Y
and complete glyph-mask hashes come from independently decoding that strike;
the capture supplies input only. The updated first-line mask for Kick 1.3
comes from issuing Text separately for each byte through the original 1.3 ROM;
its SHA-256 is `4ee6aa59a25a1e0b3eea6cdd9052f85cf3ba93225ce5bcd9d8f8be7b7c29e7e4`.
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

## Reconciliation decisions (2026-11-08)

The selected target is one practical dialect shared by both Kickstart variants,
with choices made feature by feature from the JR-Comm and SkyTERM references:

| Area | Selected contract |
| --- | --- |
| Text | Wrap by actual mapped-glyph advance before painting, independent of packet boundaries and explicit flushes. Preserve separate ANSI CR/LF, nominal BS/tabs, zero-origin coordinates, configured default font and safe fallback. |
| Kickstart rendering | Per-character text: Kick 1.3 clips each glyph's final overhang without recovering it on later characters; Kick 3.1 retains it. Preserve the verified ellipse arithmetic variants. |
| Depth | Command 17 mode 1 selects eight colours; mode 2 selects sixteen. Preserve geometry and contents, reset palette/pens to foreground 2/background 0, mask existing SkyPix pixels on reduction and never restore discarded bits on expansion. Invalid modes do nothing. |
| Palette | Use the sixteen RGB4 defaults in ANSI.TXT and SkyTERM. Command 11 retains all sixteen supplied entries at either depth. Command 12 resets palette/pens without changing depth. |
| Scrolling | Preserve normal scrolling. The earlier proposal to disable it at command 17 was withdrawn because neither original terminal establishes that behaviour. JAM1 is only a drawing mode. Command 99 enables scrolling idempotently and changes no font, palette or position; it has no disabling argument. |
| Brushes | Standard ByteRun1, including 0x80 no-op and valid 128-byte runs. Retain bounded decoding and CMAP-before-BODY validation. Captured brushes inherit active depth/palette; received brushes own their CMAP. Apply supplied colours once on first blit, preserve unspecified colours and promote display depth when needed. Failed replacement preserves the old brush. |
| Delays | Command 14 uses 50 Hz ticks; zero is a no-op, and one/two ticks are honoured. Pauses retain unread bytes, keep local UI responsive, and cancel on reset/EOF. Command 99 idempotently enables already-enabled scrolling and changes no font, palette or position; it cannot disable scrolling. |
| Retained interfaces | Existing blinking cursor overlay/capture exclusion, keyboard/modifiers, raw controller-1 mouse reports, bounded gadget definition/fill and ANSI replies. No gadget execution, hit-test feedback, controller-2 reports or drawing acknowledgments. |
| Transfers and primitives | Type-3 samples load on receipt/cache hit and command 9 plays on channel 1; type 2 remains cache-only. Preserve cache-response bytes, CRC XMODEM, duplicate-block protection, confined paths, cancellation/error handling, native mode-1 flood, outline-plus-boundary-fill ellipses and Y+1 brush capture. |

IFF/8SVX decoding, analog-filter emulation, local gadget execution, Chat Buffer, modem and AREXX functionality
remain excluded. Existing configuration and Kickstart selection values remain
stable. RoF remains the normal application test; copyright-specific regression
expectations are deferred until that investigation is resolved. The separate
Amiga 640x400 preset is implemented without changing the 200-line default.

## Supported behaviour

| Area | Policy and independent checks |
| --- | --- |
| Parsing | Bounded CSI parameters and font/asset strings; all input splits; opaque escape strings; 16-bit numeric wrapping; malformed-command recovery. SkyPix and RIP select separate parsers. |
| Lines and ellipses | Directed line ties and signed-word arithmetic; Kick 1.3 32-bit versus Kick 3.1 64-bit ellipse recurrences; ordered ROM point vectors. Work is bounded for hostile overflow cycles outside the checked reference domain. |
| Pens and fills | RGB4 palette, initial eight colours, point/line/rectangle/ellipse/flood operations and brush minterms. Command 3 uses flood mode 1 regardless of its mode argument; command 13 outlines then performs boundary flood. Command 17 selects eight/sixteen colours and restores the documented palette/pens without altering scrolling. Depth reduction masks existing native pixel/cell indices in both phases, preserving status and padding; expansion does not recover discarded bits. Command 11 retains all sixteen entries at either depth. |
| Font position controls | Native default home uses its baseline, retaining the complete strike. Command 19 uses its logical pixel anchor. Native-font CR retains Y; LF adds current height; BS subtracts nominal width without a zero clamp; HT uses eight nominal widths; VT is inert. Diamond/20 position-probe regressions and literal Ruby/12 accent rows are independent expectations. |
| Native-font wrapping | Actual mapped-glyph advance is measured before drawing. Diamond/20 fits 33 i characters on its first line, wraps the 161st, and wraps 32 W characters from X=100 to finish at X=100 on the next line. Network splits and explicit flushes give identical results. Wrap-off uses the nominal last-column origin. Comparison uses widened arithmetic before storing signed-word coordinates. Unicode revisions replace the previous advance without wrapping or retiring history twice. |
| Native-font scrolling | With full-screen margins, LF advances by the current height and clamps at the last nominal baseline, using current ascent/descent. At the bottom Diamond/20 scrolls exactly 20 pixels and its underline stays at Y=195 on a 640x200 canvas without status (Y=175 with status and twelve unused pixels below the 180-pixel text region). An off-grid overshoot scrolls the exact excess. Ruby/12 scrolls its 192-pixel text region and preserves the eight unused canvas rows. Cell-aligned moves retain cell text; fractional moves retain raster data without stale cell text. Status and canvas padding are preserved. Native two-phase stripe tests and reply-fenced application captures cover both variants/encodings/status choices. Reference: JR-Comm `H17:0ba6–0bfa`, `H17:2264–2408`. |
| Native-font erasure | ANSI EL0/1/2 use raw pixel X and the current font's ascent/descent, preserving position. The independent reference is `H16:11a0–11ca`, `H17:1c08–1dce` and geometry at `H17:0ba6–0bd0`: EL0 clears X to the canvas edge; EL1 clears through X+nominal-width-1; EL2 clears the whole strike row. Native tests check literal rectangle boundaries and both pixel planes; application tests check tcsh's BS CSI K sequence with Diamond/20. JAM2 spaces paint the full font-height background even when the compiled empty glyph has a one-row mask; named-font JAM1 spaces preserve pixels. Full proportional JAM2 horizontal-extent equivalence to Amiga ROMs remains unclaimed. |
| Cursor | Separate RGB overlay, one scanline at baseline+1; X-2 with two-pixel quantization and nominal-width masks (8 pixels for width 8, otherwise 16) on the normal HIRES canvas. Negative/off-canvas portions clip without clamping the logical position. Clear/home, proportional advances, command 19 and font changes refresh it; brush/history captures exclude it. Native tests inject 185 ms callbacks for phase, hide/show and steady mode; refresh does not restart the timer. Palette loads do not recolour the captured cursor RGB. UI snapshots and CustomCursor preservation retain it. |
| Fonts and text | Configured initial/reset Amiga strike (Pearl 8 by default); exact installed strike, exact compiled strike, then configured default for unavailable remote requests. Initial unavailable preferences fall back to Pearl 8; no strike stretching. Native baseline, signed kerning/spacing, monospaced centring and proportional advances. Font selection, including fallback, uses JAM1; reset uses JAM2. Every byte is rendered separately: Kick 1.3 clips that glyph's trailing overhang permanently; Kick 3.1 retains it. |
| Mouse | Left-button-down sends the literal `ESC[21;1;x;y!` with native zero-based canvas pixels and no newline; independent of gadgets or host enable commands. Releases, other buttons, motion, status/padding and UI-handled events do not report. Local Alt/Command-O and the online menu gate forwarding; the normal status bar shows bright/dim M. Disabled events retain button synchronization; wheel-up opens local scrollback. Both variants/encodings/status choices and drawable scaling have independent application tests. |
| Gadget definitions | Command `22;n;c;x1;y1;x2;y2!` retains five 16-bit words in slot 1–20 and paints the inclusive rectangle with the current foreground pen using command 4's clipping rules. Replacing a record paints the new rectangle without erasing old pixels. Invalid slots or parameter counts have no record/drawing effect. Font/palette/display clears retain records; full terminal reset clears them. Native tests cover all slots, every input split, numeric wrapping and clipping; application tests check both pixel planes and raw mouse reports inside/outside the rectangle. No acknowledgment, button decoration or stored-command execution is added. Reference: supplied protocol's portable-implementation section and JR-Comm `H16:2564–25da`. |
| Brush capture | Command 6 captures starting at Y+1 with the active depth and palette. Copy uses source plane depth and does not clear unrelated destination planes. Received and captured brush palettes apply once on first blit; a deeper brush promotes the display to sixteen colours. Supplied palette entries replace current entries; unspecified entries remain intact. Invalid replacement retains prior pixels, depth, palette and pending state. |
| ILBM | Raw and standard ByteRun1, including 0x80 no-op and 128-byte literal/repeat runs. CMAP must precede BODY and is retained with the brush, quantized to RGB4 using the upper nibble of each component. Dimensions come from BMHD, not the transfer request. One to four planes and a four-million-pixel decoded limit. Mask planes are unsupported. |
| Delays | Command 14 uses 50 Hz ticks; zero is a no-op, and one/two ticks are honoured. Pauses retain unread bytes, keep local UI responsive, and cancel on reset/EOF. Command 99 idempotently enables already-enabled scrolling and changes no font, palette or position; it cannot disable scrolling. |
| Sound | Type 3 has a six-byte big-endian header (uint32 PCM count, uint16 rate), followed by signed 8-bit mono PCM. Positive lengths up to 4 MiB and nonzero rates are accepted; odd lengths are valid and trailing padding is ignored. Command 9 ignores speed/start/end and uses its loop count; zero deliberately plays once rather than waiting indefinitely as the originals do. A bounded incremental linear converter uses the native 16-bit period `(3579545 / rate) & 65535`, preserves phase across blocks/repetitions and feeds shared channel 1 at 44100-Hz stereo with existing gain/pan. Remote parsing waits through playback and backend latency; local UI remains responsive. Connected, menu and picker input waits service the owner at bounded intervals without parsing remote input. Disabled/unavailable/failed output releases the hold. Finish/EOF cancels playback; reset/destruction unloads samples. No physical Paula waveform, analog filter or device-chunk-gap emulation is claimed. |
| Transfers | Command 16 accepts brush mode 1, IFF sound mode 2, FutureSound mode 3 and general-file mode 20 using shared native CRC XMODEM workers with independent SkyPix state. All types share the active BBS's `skypix` directory, matching SkyTERM's BRU: destination; JR-Comm instead uses its configured download directory for mode 20. Modes 2/3/20 preserve cached bytes, including XMODEM padding, without brush replacement. Type 3 also loads signed mono PCM after cache publication or on a cache hit; a failed load leaves the file cached and retains the old sound. Type 2 has no IFF sound decoder. The second filename bang is the exact transfer boundary; no RIP CRLF skipping. Cache hits send ten CAN, ten backspaces and CR. Only completed files publish, after ILBM validation for mode 1. Failure/cancellation discards partial files and preserves the previous brush. Other modes remain declined. |
| Font manager | Menu-only picker installs complete `.font` families beneath `fonts/amiga/` beside the active INI. All referenced strikes validate before replacement. Removal cannot remove compiled fonts. Selected fonts own their loaded data and survive package replacement/removal. Metadata queries expose errors without eagerly copying unused Wren values. |

Sound playback, sound transfers and colour/outline disk fonts remain unsupported.
Raw mouse reports do not depend on gadget definitions. Controller-2 menu reports
and local execution of stored gadget commands remain unestablished in the supplied
JR-Comm analysis and are not implemented.
Unsupported sound-transfer requests are declined with
the ordinary cancellation response. This implementation deliberately bounds
malformed arguments rather than reproducing JR-Comm stale-parameter reads or
memory corruption.

## Remaining text compatibility gaps

The updated JR-Comm research describes behaviour beyond this checked subset:
ANSI movement in eight-pixel steps with non-eight-pixel fonts and mapping
custom ANSI scrolling margins into the native pixel model. ANSI positioning
still uses the screen-mode cell grid. The ordinary
cell path for a fixed, grid-aligned default font retains cell-based wrapping;
the native-font path uses measured advances. Glyphs are painted
per character; tests establish packet/flush independence, not an Amiga serial
arrival schedule. JR-Comm's former forecast/count wrapping is intentionally
superseded by the measured-advance policy. The software cursor implements the documented normal HIRES
mapping, masks, colour isolation and a requested 185 ms phase interval; it is
not an Amiga DMA/Copper/timer.device implementation. Other hardware display
mappings and exact VBLANK/task scheduling remain outside these tests. Local UI
selects its ordinary cell cursor and snapshots restore the terminal overlay.

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
| `font_catalog` | Literal Pearl g/p/y rows retain descenders in fixed console cells. |
| `amiga_font_transactions` | Injected rename/rollback failures and secondary cleanup failures before/after the package publication boundary. |
| `cterm_skypix_audio` | Independent native raw vector, literal pre-mixer linear PCM, signed-byte alphabet, quantized periods, varying pull/block sizes and repeat phase; odd/max/truncated/zero/oversized data, allocation/open/backend failures, latency, bounded long-sample playback, channel-1 routing, zero-as-one, parser split/holds, previous-sample retention, reset/EOF/shutdown and active destruction. Pull/fake backends only. |
| `cterm_skypix` | Parser splits, command/state/pixel expectations, both variants, status geometry, resets, delays, proportional overhang, standard ByteRun1 boundaries/no-op, brush palette/depth ownership and malformed replacement, palette/depth transitions, short delays, and literal Diamond cursor positions/timer transitions. |
| `skypix_rom_vectors` | Independent ordered point sets/hashes and Diamond 20 text for the retained ROM vectors. |
| `syncterm_skypix_conformance` | Real application over loopback TCP, both variants with status on/off, fixed mode resolution/aspect/system grid, status within the selected mode, Current/Custom/non-Amiga geometry and padding, and Latin-1/CP437/UTF-8; literal Pearl A/g/p/y rows, synthetic C1 glyphs, Ruby 12 metrics, editor save/reload and system-font UI isolation; both variants with Diamond 20 in CP437/UTF-8 on Amiga 640x200/640x400, proportional cursor movement and unscaled 8x8 menu rasters; literal native glyph rows below Y=199 and exact mode/status clipping; palette/brush pixels, transfers/cache/error recovery, responsive delays, reset/EOF and disconnected scrollback. Replies fence one coherent capture of related cells, pixels, palette and state. |
| `syncterm_skypix_welcome` | RoF's captured Times/15 network lines remain at their pixel anchors; independent native-strike hashes check every glyph pixel in both planes, palette and cursor, both variants/encodings/status choices, split input and reset/replay. |
| `syncterm_skypix_cache` | Types 2/3 upload opaque bytes with LF/NUL/high bytes and padding; SOH/STX, bad-CRC retry, duplicate protection, shared-namespace cache hits across types 2/3/20, path rejection, unsupported modes, cancellation/EOF cleanup and terminal-input recovery. Both variants/encodings/status choices; one reply-fenced capture checks retained brush pixels in both planes and unchanged palette. Also covers successful and failed type-3 loads, cached sample replacement, odd lengths, type-2/20 isolation and reset. Physical audio remains disabled, so this fixture verifies muted recovery rather than audible playback. |
| `syncterm_skypix_mouse` | Exact documented click bytes, zero-origin/sub-cell precision and scaled drawable mapping; no report on releases/motion/other buttons/status/local UI; bright/dim M and both toggle routes, disabled-release recovery, local wheel scrollback and reset; both variants/encodings/status choices. |
| `syncterm_skypix_scrolling` | Literal four-pixel colour stripes through exact Diamond/20 scrolls, stable bottom cursor, partial-strip joins, live/disconnected/detached viewers, both variants/encodings/status choices. Expected pixels come from fixture rectangles, not emulator captures. |
| `syncterm_graphics_scrollback` | Shared ANSI/RIP/SkyPix image history, both phases, palette changes, composition/overlays, remote Pearl descenders, status choices and disconnected/offline viewing. |
| `syncterm_amiga_font_manager` | Real picker cancellation, install, replacement confirmation, stale snapshots, metadata, removal and malformed family rejection. |
| `wren_host` | A drained long-sample queue is refilled while connected and menu VMs wait for local input, with a hidden pixel cursor and preserved caller deadline; connected destruction unlinks the borrowed polling owner. |
| `bbs_list`, `wren_menu_contract` | All Graphics values, legacy RIP fallback/canonical save, locked merge/default persistence and conflicts, menu catalogs, constants and immutable family metadata. |

Headless fixtures disable physical audio with `AudioModes=0`. Bitmap and native
algorithm tests make no claim about physical Amiga hardware, audible sound or
native display backend behaviour. ANSI CP437/UTF-8 and historical RIP suites
remain separate regressions with their original reference provenance.
