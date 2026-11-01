# UNSCII font data

The generated assets come from [UNSCII](https://github.com/viznut/unscii),
commit `257b4dfeb50651510fe8b9cf4f151a148a4490e4`.  The project describes
UNSCII as public domain or CC0.  Only `fontfiles/unscii-8.hex` and
`fontfiles/unscii-16.hex` are used; `unscii-16-full` is deliberately excluded.
SyncTERM uses the public-domain option; the upstream notice is retained in
`LICENSE.UNSCII` and the in-program SyncTERM Settings > Licenses viewer.

Exact source URLs and SHA-256 digests are recorded in `update_fonts.py`.

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
resampled in premultiplied RGBA before conversion to straight RGBA. Colour
is attached to default emoji presentation, official VS16 sequences, and
supported multi-character emoji; VS15 and default text presentation remain
monochrome. Every glyph retains a monochrome bitmap for the runtime setting.
Identical source artwork at the same presentation width shares pixel data;
presentation mappings keep separate glyph IDs wherever colour differs.
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

## Colour RLL prototype

The console also accepts compressed colour glyphs when their `row_stride` is
zero. Each glyph starts with a little-endian 16-bit byte offset, measured from
the glyph's first byte, to its right-half stream. The left stream follows the
two-byte header. Each half traverses columns from left to right, with pixels
top to bottom within each column. The left half contains `width / 2` columns;
the right contains the remaining columns. Runs must stop at the half boundary.

A nonzero alpha byte followed by red, green, and blue represents one pixel.
Zero followed by a count byte represents `count + 1` transparent pixels, up to
256. Partially transparent pixels retain all four components; RGB beneath zero
alpha is discarded. Registration validates both streams, their exact decoded
lengths, and the half offset before the renderer uses them. Glyph lookup uses
the existing sorted descriptors. Drawing streams directly from compressed
data, with no expanded glyph allocation or cache.

The checked-in font assets remain RGBA for this prototype. The CMake
`st_emoji_rll_test` target encodes them in memory, preserving shared artwork,
and compares both raster planes against RGBA for every colour glyph ID at
heights 8, 14, and 16, including decorations and independent half redraws.
The console colour-glyph test separately checks expected colours, alpha
blending, expansion gaps, horizontal/vertical scaling, odd source widths,
double-height cells, copied font ownership, and malformed streams.

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
renderer with the column-major RLL renderer; they do not isolate token decode
cost or measure GUI presentation.

Measured on 2026-11-01, FreeBSD amd64, Intel Xeon D-1541 at 2.10 GHz, CMake
Release builds with Clang 19.1.7 and GCC 13.5.0:

| Raster | Unique RGBA bytes | RLL bytes, including offsets | Saving |
|---|---:|---:|---:|
| 16x8 | 1,959,936 | 932,418 | 52.43% |
| 16x14 | 3,429,888 | 1,956,372 | 42.96% |
| 16x16 | 3,919,872 | 2,464,218 | 37.14% |

Median CPU milliseconds per frame (RLL/RGBA ratio below 1 is faster):

| Compiler | Raster | Redraw | RGBA ms | RLL ms | RLL/RGBA |
|---|---|---|---:|---:|---:|
| Clang | 16x8 | Full | 2.180 | 2.094 | 0.961 |
| Clang | 16x8 | Left | 1.111 | 1.077 | 0.970 |
| Clang | 16x8 | Right | 1.082 | 1.066 | 0.984 |
| Clang | 16x14 | Full | 3.556 | 3.318 | 0.933 |
| Clang | 16x14 | Left | 1.776 | 1.646 | 0.927 |
| Clang | 16x14 | Right | 1.761 | 1.644 | 0.933 |
| Clang | 16x16 | Full | 4.087 | 3.820 | 0.935 |
| Clang | 16x16 | Left | 1.972 | 1.832 | 0.929 |
| Clang | 16x16 | Right | 1.967 | 1.820 | 0.925 |
| GCC | 16x8 | Full | 1.889 | 1.992 | 1.054 |
| GCC | 16x8 | Left | 0.915 | 1.010 | 1.104 |
| GCC | 16x8 | Right | 0.941 | 0.990 | 1.051 |
| GCC | 16x14 | Full | 3.102 | 3.080 | 0.993 |
| GCC | 16x14 | Left | 1.426 | 1.468 | 1.029 |
| GCC | 16x14 | Right | 1.476 | 1.501 | 1.017 |
| GCC | 16x16 | Full | 3.560 | 3.513 | 0.987 |
| GCC | 16x16 | Left | 1.630 | 1.686 | 1.034 |
| GCC | 16x16 | Right | 1.687 | 1.719 | 1.019 |

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
