# UNSCII font data

The generated assets come from [UNSCII](https://github.com/viznut/unscii),
commit `257b4dfeb50651510fe8b9cf4f151a148a4490e4`.  The project describes
UNSCII as public domain or CC0.  Only `fontfiles/unscii-8.hex` and
`fontfiles/unscii-16.hex` are used; `unscii-16-full` is deliberately excluded.
SyncTERM uses the public-domain option; the upstream notice is retained in
`LICENSE.UNSCII` and the in-program SyncTERM Settings > Licenses viewer.

Exact source URLs and SHA-256 digests are recorded in `update_fonts.py`.

`box_drawing.py` applies SyncTERM's CP437-compatible box drawing overrides
before generating the 8x8 and 8x16 banks and before overlaying UNSCII on
Unifont. All 40 box drawing characters present in CP437 borrow the exact
rasters from built-in selector zero in `st_builtin_font_data.c`; that source
and its byte glyphs are unchanged. Additional light, heavy, mixed-weight,
dashed and rounded boxes use the same stroke positions. Light strokes are
one pixel horizontally and two vertically; heavy strokes expand by one pixel
on either side, to three horizontally and four vertically. Horizontal
strokes use row 4 at 8x8 and row 7 at 8x16 (zero-based); vertical light
strokes use columns 3 and 4. The original three diagonal glyphs are retained.
The project-owned overrides retain the BSD-2-Clause copyright notice.
The 8x14 fallback continues to use the built-in CP437 font.

The `box_drawing_generator` and `fonts` CTests check independent reference
rasters and exact agreement with CP437. With the control backend enabled,
`syncterm_box_drawing_CP437` and `syncterm_box_drawing_UTF-8` check rendered
cells and both pixel phases at 8x8 and 8x16, including reset. For a visual
sample from the application, run:

```
python3 src/tests/st_box_drawing_test.py build/bin/syncterm UTF-8 \
    --reports build/reports/test-control --preview-dir /tmp/box-drawing
```

This saves PPM captures from the bitmap control backend without opening a
window or enabling physical audio.

# SyncTERM Emoji

`generated/st_emoji_data.c` contains **SyncTERM Emoji**, rasterized from the
monochrome Noto Emoji 3.002 variable font at weight 700 and the colour
Noto Color Emoji 2.051 bitmap font, under SIL OFL 1.1.
The copyright notice and full license are in `LICENSE.NOTO-EMOJI` and are
always included in **SyncTERM Settings > Licenses**, including builds with
Unifont disabled. Original SyncTERM code retains its existing BSD license.

The source is pinned to google/fonts commit
`8b0a1d0f5983c89bc2b93f1b5fb55f9e252744b5`, file
`ofl/notoemoji/NotoEmoji[wght].ttf`, SHA-256
`de6c18832938afc99caf132b39d6a30a19bac7f2e812e28db2535b4608d27551`.
The colour source is `googlefonts/noto-emoji`, tag `v2.051`, file
`fonts/NotoColorEmoji.ttf`, SHA-256
`72a635cb3d2f3524c51620cdde406b217204e8a6a06c6a096ff8ed4b5fd6e27b`.
It is available locally on many systems as `NotoColorEmoji.ttf`.
`update_emoji.py` shapes the Unicode 18 emoji repertoire with HarfBuzz and
includes sequences resolving to a single supported source glyph, including
skin tones, ZWJ sequences, flags, keycaps, and presentation selectors.
Unicode input versions, URLs, and digests reuse the pinned width and grapheme
generators; official Unicode 18 variation sequences are pinned in
`update_emoji.py`; the Unicode License v3 notice is in `../../text/unicode_text/LICENSE.UNICODE`.

The rasterizer crops the source ink, fits it proportionally to the cell,
resamples with Lanczos, and thresholds coverage at 80/255. The 8-pixel font
uses the entire cell; 14- and 16-pixel fonts reserve a one-pixel vertical
margin, and double-column forms also reserve a one-pixel horizontal margin.
Single-column forms use 8-pixel bitmaps and double-column forms use 16-pixel
bitmaps. Colour PNGs are extracted from the source CBDT strike with fontTools and
resampled in premultiplied RGBA before conversion to straight RGBA. Each colour
glyph has independent square-pixel and 6:5 tall-pixel projections, both
resampled directly from the source artwork. Fitting happens in physical space
before projection into pixels, so short cells can use more horizontal detail.
Both versions use the transparent-background ARGB RLL format described below;
identical encoded artwork shares storage across glyphs and aspect variants. Colour
is attached to default emoji presentation, official VS16 sequences, and
supported multi-character emoji; VS15 and default text presentation remain
monochrome. Every glyph retains a monochrome bitmap for the runtime setting.
Presentation mappings keep separate glyph IDs wherever colour differs.
Ordinary builds consume the checked-in C arrays and need no font libraries
or network access. To regenerate, install fontTools, Pillow with RAQM support and a
HarfBuzz shared library, then run:

```
python3 src/media/fonts/update_emoji.py
```

Use `--font /path/to/NotoEmoji.ttf` for a matching local font and
`--color-font /path/to/NotoColorEmoji.ttf` for the colour source and
`--source-dir /path/to/unicode-inputs` for cached Unicode inputs. All inputs
are checked against the pinned SHA-256 values. The generated raster output
can also depend on the Pillow, FreeType, and HarfBuzz versions used; normal
builds do not rerasterize it.
The checked-in data was generated with Pillow 12.3.0, FreeType 2.14.3,
RAQM 0.11.0, and HarfBuzz 14.6.0.

The emoji rasterizer also refreshes the picker coverage manifest and catalog.
All font and Unicode update scripts check that the picker remains consistent;
see `../../text/emoji/UPSTREAM.md` for pinned English names and regeneration.

## Colour RLL storage and profiling

The generated colour banks use compressed glyphs with `row_stride` zero.
Each glyph starts with a little-endian 16-bit byte offset, measured from
the glyph's first byte, to its right-half stream. The left stream follows the
two-byte header. Each half traverses its row spans from left to right, then
top to bottom. The left half contains `width / 2` columns; the right contains
the remaining columns. For the bundled wide glyphs these spans are eight
pixels wide. Runs may cross rows within a half, but must stop at the half
boundary.

A nonzero alpha byte followed by red, green, and blue represents one pixel.
Zero followed by a count byte represents `count + 1` transparent pixels, up to
256. Partially transparent pixels retain all four components; RGB beneath zero
alpha is discarded. Registration validates both streams, their exact decoded
lengths, and the half offset before the renderer uses them. Glyph lookup uses
the existing sorted descriptors. Drawing streams directly from compressed
data, with no expanded glyph allocation or cache.

Each colour descriptor has a square-pixel stream and an optional `tall_rgba`
stream with its own byte count, using the same dimensions and encoding.
Registration validates and, when copying fonts, owns both streams.
The renderer selects the nearer projection using the effective physical pixel
height/width ratio: native raster width times display aspect height, divided
by native raster height times display aspect width. Ratios at least 11:10
select the 6:5 bank; smaller ratios select the square bank. Disabling aspect
correction selects square pixels. This covers normal 80x25 versus LCD 80x25
and 80x30, and respects custom aspect settings. The two banks are an
approximation for modes with other pixel shapes. Aspect-only changes
invalidate retained text while preserving explicit pixel graphics.

The CMake `st_emoji_rll_test` target independently expands each generated
stream to a raw RGBA reference, preserving shared artwork, and compares both
raster planes for every colour glyph ID at all three heights and both
aspects, including decorations and independent half redraws.
The console colour-glyph test separately checks expected colours, alpha
blending, expansion gaps, horizontal/vertical scaling, odd source widths,
double-height cells, copied font ownership, malformed streams, aspect-only
redraw, mode reconfiguration and explicit graphics preservation. Generator
unit tests independently check fit dimensions and RLL packet bytes; they are
skipped when Pillow or fontTools is unavailable. `syncterm_emoji_aspect` checks
source-derived expected grinning-face dimensions through coherent control
captures at both pixel aspects and all three heights, including reset.

From the repository root:

```
cmake -S . -B build-rll -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DST_CONSOLE_TEST=ON
cmake --build build-rll --target st_emoji_rll_test st_console_color_glyph_test
ctest --test-dir build-rll -R '^(emoji_rll|st_console_color_glyph)$' --output-on-failure
build-rll/src/media/fonts/st_emoji_rll_test --benchmark
```

The benchmark uses a headless 80x25 console, seven samples of forty frames,
alternating which format runs first. It reports median and minimum/maximum
process CPU time, including all process threads. Encoding, registration,
warmup and correctness captures are outside the timed region. Full redraws
cycle through the entire colour repertoire. Half redraws change background
attributes on one side of a fixed screen of 1,000 emoji, leaving the other
side unchanged. Both formats use the same cells and backgrounds. These are
end-to-end cell update/raster timings comparing the existing row-major RGBA
renderer with the half-span RLL renderer; they do not isolate token decode
cost or measure GUI presentation.

The deployed dual-aspect banks contain 3,832 colour glyph IDs per height.
Generation from the pinned inputs was reproduced byte for byte in a separate
output tree. Every square-pixel visible RGBA sample, monochrome bitmap and
scalar/cluster mapping was checked against the previous checked-in data.
Only RGB beneath zero alpha is discarded by RLL. Picker names and eligibility
are unchanged; its coverage fingerprint is regenerated with the font data.

| Height | Previous single-aspect RGBA bytes | Both aspect banks, shared RLL bytes |
|---|---:|---:|
| 8 | 1,959,936 | 2,187,478 |
| 14 | 3,429,888 | 4,185,248 |
| 16 | 3,919,872 | 4,891,940 |
| Total | 9,309,696 | 11,264,666 |

These counts include half offsets and shared encoded payloads, excluding
lookup descriptors and the monochrome banks. Both colour aspects together
cost 21.0% more payload than the previous single uncompressed aspect.

Historical prototype measurements below used the square-pixel bank before
cross-artwork payload deduplication. They compare raw and compressed drawing;
they do not include the later aspect-selection change. Measured on 2026-11-01,
FreeBSD amd64, Intel Xeon D-1541 at 2.10 GHz, CMake Release builds with
Clang 19.1.7 and GCC 13.5.0:

| Raster | Unique RGBA bytes | Column RLL bytes | Half-span RLL bytes | Half-span saving |
|---|---:|---:|---:|---:|
| 16x8 | 1,959,936 | 932,418 | 1,017,112 | 48.10% |
| 16x14 | 3,429,888 | 1,956,372 | 2,027,048 | 40.90% |
| 16x16 | 3,919,872 | 2,464,218 | 2,542,354 | 35.14% |

Both compressed sizes include the half offsets. The initial column-major
prototype and its benchmark are retained in commit `08383a3`. The half-span
prototype trades slightly larger data for contiguous raster writes.

Median CPU milliseconds per frame for the half-span prototype:

| Compiler | Raster | Redraw | RGBA ms | RLL ms | RLL/RGBA |
|---|---|---|---:|---:|---:|
| Clang | 16x8 | Full | 1.979 | 1.704 | 0.861 |
| Clang | 16x8 | Left | 0.987 | 0.863 | 0.874 |
| Clang | 16x8 | Right | 0.966 | 0.830 | 0.859 |
| Clang | 16x14 | Full | 3.202 | 2.768 | 0.864 |
| Clang | 16x14 | Left | 1.605 | 1.377 | 0.858 |
| Clang | 16x14 | Right | 1.613 | 1.375 | 0.853 |
| Clang | 16x16 | Full | 3.643 | 3.174 | 0.871 |
| Clang | 16x16 | Left | 1.796 | 1.549 | 0.862 |
| Clang | 16x16 | Right | 1.795 | 1.563 | 0.871 |
| GCC | 16x8 | Full | 1.849 | 1.768 | 0.956 |
| GCC | 16x8 | Left | 0.897 | 0.873 | 0.974 |
| GCC | 16x8 | Right | 0.895 | 0.869 | 0.971 |
| GCC | 16x14 | Full | 3.068 | 2.899 | 0.945 |
| GCC | 16x14 | Left | 1.424 | 1.421 | 0.998 |
| GCC | 16x14 | Right | 1.426 | 1.431 | 1.003 |
| GCC | 16x16 | Full | 3.504 | 3.339 | 0.953 |
| GCC | 16x16 | Left | 1.607 | 1.637 | 1.019 |
| GCC | 16x16 | Right | 1.622 | 1.671 | 1.030 |

Comparison of each layout against its own raw RGBA baseline. Each entry is
the RLL/RGBA ratio for full / left / right redraws; below 1 is faster:

| Compiler | Raster | Column ratios | Half-span ratios |
|---|---|---|---|
| Clang | 16x8 | 0.961 / 0.970 / 0.984 | 0.861 / 0.874 / 0.859 |
| Clang | 16x14 | 0.933 / 0.927 / 0.933 | 0.864 / 0.858 / 0.853 |
| Clang | 16x16 | 0.935 / 0.929 / 0.925 | 0.871 / 0.862 / 0.871 |
| GCC | 16x8 | 1.054 / 1.104 / 1.051 | 0.956 / 0.974 / 0.971 |
| GCC | 16x14 | 0.993 / 1.029 / 1.017 | 0.945 / 0.998 / 1.003 |
| GCC | 16x16 | 0.987 / 1.034 / 1.019 | 0.953 / 1.019 / 1.030 |

The half-span version improves GCC's 8-pixel bank from a 5–10% regression
to a 3–4% improvement. Its 16-pixel full redraw is about 5% faster, while
isolated half redraws remain about 2–3% slower. Clang improves about 13–15%
across these workloads. This supports the contiguous-write layout, but does
not establish which compiler optimization accounts for the difference.

# Legacy built-in fonts

`st_builtin_font_data.c` retains SyncTERM's complete legacy built-in font
catalog: selectors 0 through 45, every available 8x8, 8x14, 8x16, and 12x20
bitmap, and the corresponding Unicode mappings. This is project-owned data
carried forward from the legacy `conio/allfonts.c` and
`conio/utf8_codepages.c`, not generated external data.

When the catalog registers an 8x8 or 8x14 built-in font, it appends the
presentation font's narrow and wide missing-glyph boxes and decode-error
glyph. The original 256 byte glyphs and their mappings keep their indexes.
This supplies tofu even when the startup font in console slot zero has a
different height.

Unicode fallback fonts append the size-matched emoji banks at 8x8, 8x14,
and 8x16. Monochrome uses the existing Unifont, UNSCII, or codepage glyph
whenever available; Noto supplies missing monochrome glyphs and colour emoji.
Native monochrome and Noto colour can share a Unicode presentation variant
without replacing the original byte glyph or changing its cell width when
colour is toggled. Emoji presentation uses two cells; an eight-pixel native
monochrome outline is centered in that space without stretching. Text
presentation keeps its native width.
Each console designates one shared fallback per pixel size, reusing it across
catalog reloads.
BBS fonts retain native Unicode mappings and all original byte glyph indexes.
Mode changes keep emoji and tofu available through a fallback with matching
dimensions, even when font zero retains the startup mode's dimensions.
