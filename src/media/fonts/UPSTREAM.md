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
colour is toggled. Colour artwork is sampled to that native width when needed.
Each console designates one shared fallback per pixel size, reusing it across
catalog reloads.
BBS fonts retain native Unicode mappings and all original byte glyph indexes.
Mode changes keep emoji and tofu available through a fallback with matching
dimensions, even when font zero retains the startup mode's dimensions.
