# SkyPix compatibility

The Graphics selector provides **SkyPix (Kick 1.3)** and **SkyPix (Kick 3.1)**
independently of RIP. Both follow JR-Comm 1.02a command behaviour. The variant
selects the documented Kickstart ellipse arithmetic and proportional text
overhang rules. It does not select a different wire protocol.

Selecting SkyPix chooses the 640x200, 4:3 canvas and Latin-1 as initial
preferences. Screen Mode, Encoding and Font remain editable and configure the
connection. Screen Mode selects pixel geometry/aspect; the installed or bundled
Amiga strike supplies nominal cell width/height. The text grid rounds up to
complete cells, with graphics clipped to the exact selected canvas. The optional
status row is outside the canvas and uses the same pixel scale. Ordinary Font
preferences remain available outside SkyPix. Disconnected scrollback retains
connected geometry; returning to the directory restores its geometry.

Application UI, including status, online menu, file picker and transfer dialogs,
uses system CP437 rather than the connection's Amiga font. Exact system strikes
are used at their supported sizes; other cell dimensions scale a system strike
through the console renderer. Fixed-cell conversion preserves the entire Amiga
bitmap, including rows below the baseline. Pixel-positioned JR-Comm text retains
its separate native baseline convention. Classic bitmap fonts carry byte indexes,
including C1 glyph positions, without a charset tag; the selected codepage maps
bytes to glyphs, while UTF-8 text uses Latin-1 glyph mapping. Remote SkyPix palette
changes do not recolour application UI. SkyPix drawing and text publish identically
into both blink phase planes.

## Reference provenance

The implementation follows the user-supplied `SKYPIX_PROTOCOL.adoc` and
`SKYPIX_ALGORITHMS.adoc`, supplied in `~/SkyPix/docs/`, with disassembly and ROM
probe results in the same research tree. These supersede conflicting claims in
the earlier `ANSI.TXT` description. JR-Comm executable SHA-256:
`8c2e140158531c9095b0439150c887af34d7c938f24045eb44f383d307db0fe6`.

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

The 2026-11-05 rendering update also supplies 33 position-probe checks and
45 cursor-probe checks under `~/SkyPix/disassembly/`, driven by the original
JR-Comm instructions. They distinguish nominal BS/HT width from proportional
advance, normal CR/LF, font-height anchors, ANSI's eight-pixel positioning and
receive-buffer-dependent wrapping. These recorded-call probes do not rasterize
full-screen scrolling or establish physical sprite/timer behaviour.

## Supported behaviour

| Area | Policy and independent checks |
| --- | --- |
| Parsing | Bounded CSI parameters and font/asset strings; all input splits; opaque escape strings; 16-bit numeric wrapping; malformed-command recovery. SkyPix and RIP select separate parsers. |
| Lines and ellipses | Directed line ties and signed-word arithmetic; Kick 1.3 32-bit versus Kick 3.1 64-bit ellipse recurrences; ordered ROM point vectors. Work is bounded for hostile overflow cycles outside the checked reference domain. |
| Pens and fills | RGB4 palette, initial eight colours, point/line/rectangle/ellipse/flood operations and brush minterms. Command 3 uses flood mode 1 regardless of its mode argument; command 13 outlines then performs boundary flood. Command 17 resets pens/palette without changing depth. |
| Font position controls | Native default home uses its baseline, retaining the complete strike. Command 19 uses its logical pixel anchor. Native-font CR retains Y; LF adds current height; BS subtracts nominal width without a zero clamp; HT uses eight nominal widths; VT is inert. Diamond/20 position-probe regressions and literal Ruby/12 accent rows are independent expectations. |
| Fonts and text | Configured initial/reset Amiga strike (Pearl 8 by default); exact installed strike, exact compiled strike, then configured default for unavailable remote requests. Initial unavailable preferences fall back to Pearl 8; no strike stretching. Native baseline, signed kerning/spacing, monospaced centring and proportional advances. Font selection, including fallback, uses JAM1; reset uses JAM2. Kick 1.3 clips final overhang, which subsequent text can reveal; Kick 3.1 retains it. |
| Brush capture | Command 6 captures starting at Y+1. Copy uses source plane depth and does not clear unrelated destination planes. |
| ILBM | Raw and JR-Comm ByteRun1, including 0x80 repeating 129 bytes; CMAP required before BODY but not applied. Dimensions come from BMHD, not the transfer request. One to four planes and a four-million-pixel decoded limit. Mask planes are unsupported. |
| Delays | Command 14 uses 50 Hz ticks; 0..2 ignored. Pauses retain unread bytes, keep local UI responsive, and cancel on reset/EOF. Command 99 has no special graphics effect. |
| Transfers | Brush mode 1 and general-file mode 20 use shared native CRC XMODEM workers with independent SkyPix state. The second filename bang is the exact transfer boundary; no RIP CRLF skipping. Cache hits send ten CAN, ten backspaces and CR. Only completed valid assets publish to the active BBS's `skypix` directory. Failure/cancellation discards partial files and preserves the previous brush. |
| Font manager | Menu-only picker installs complete `.font` families beneath `fonts/amiga/` beside the active INI. All referenced strikes validate before replacement. Removal cannot remove compiled fonts. Selected fonts own their loaded data and survive package replacement/removal. Metadata queries expose errors without eagerly copying unused Wren values. |

Sound playback, sound transfers, interactive gadgets and colour/outline disk
fonts remain unsupported. Unsupported sound-transfer requests are declined with
the ordinary cancellation response. This implementation deliberately bounds
malformed arguments rather than reproducing JR-Comm stale-parameter reads or
memory corruption.

## Remaining text compatibility gaps

The updated JR-Comm research describes behaviour beyond this checked subset:
ANSI movement in eight-pixel steps with non-eight-pixel fonts, the exact
receive-buffer forecast/count wrapping rule, and native font ascent/descent
handling for full-screen margin scrolling after remote font changes. The
current terminal still uses its normal cell grid for ANSI positioning and
ordinary wrapping. It does not reproduce the Amiga hardware sprite's
quantized position, two width masks, separate colour or 185 ms timer callback
state machine; it uses the host console cursor. These are compatibility gaps,
not behaviours established by the existing glyph/vector tests.

Graphics history uses the shared optional pixel-strip path, including text
rendered through native Amiga fonts. Remote-font line feeds scroll enough host
cell strips to keep the current strike inside the canvas. This preserves the
rendered image; it does not claim the exact JR-Comm ascent/descent margin rule
listed above. Both blink planes and captured colours survive viewing and
reopening history after disconnect. Pixel-only text retains its raster rather
than acquiring invented character-cell content.

## Test inventory

Enable `ST_CONSOLE_TEST`, configure/build with CMake, then run:

```sh
ctest --test-dir <build> -R 'amiga_font|cterm_skypix|skypix_rom_vectors|syncterm_skypix_conformance|syncterm_amiga_font_manager' --output-on-failure
```

| Test | Coverage |
| --- | --- |
| `amiga_font` | HUNK relocation, zero-offset references, metrics, bit rasters, malformed input, descriptors and confined paths; compiled catalog, package lifecycle and retained loaded fonts. |
| `font_catalog` | Literal Pearl g/p/y rows retain descenders in fixed console cells. |
| `amiga_font_transactions` | Injected rename/rollback failures and secondary cleanup failures before/after the package publication boundary. |
| `cterm_skypix` | Parser splits, command/state/pixel expectations, both variants, status geometry, resets, delays, proportional overhang and native ILBM decoding. |
| `skypix_rom_vectors` | Independent ordered point sets/hashes and Diamond 20 text for the retained ROM vectors. |
| `syncterm_skypix_conformance` | Real application over loopback TCP, both variants with status on/off, selected canvas geometry and Latin-1/CP437/UTF-8; literal Pearl A/g/p/y rows, synthetic C1 glyphs, Ruby 12 metrics, editor save/reload and system-font UI isolation; palette/brush pixels, transfers/cache/error recovery, responsive delays, reset/EOF and disconnected scrollback. Replies fence one coherent capture of related cells, pixels, palette and state. |
| `syncterm_graphics_scrollback` | Shared ANSI/RIP/SkyPix image history, both phases, palette changes, composition/overlays, remote Pearl descenders, status choices and disconnected/offline viewing. |
| `syncterm_amiga_font_manager` | Real picker cancellation, install, replacement confirmation, stale snapshots, metadata, removal and malformed family rejection. |
| `bbs_list`, `wren_menu_contract` | All Graphics values, legacy RIP fallback/canonical save, locked merge/default persistence and conflicts, menu catalogs, constants and immutable family metadata. |

Headless fixtures disable physical audio with `AudioModes=0`. Bitmap and native
algorithm tests make no claim about physical Amiga hardware, audible sound or
native display backend behaviour. ANSI CP437/UTF-8 and historical RIP suites
remain separate regressions with their original reference provenance.
