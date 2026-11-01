#!/usr/bin/env python3
"""Generate monochrome and colour emoji for SyncTERM's 8-pixel cell fonts.

Regeneration needs fontTools, Pillow with RAQM, and HarfBuzz. Normal
SyncTERM builds use the checked-in C data and need none of these tools.
"""

# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

import argparse
import ctypes as ct
import ctypes.util
import hashlib
import importlib.util
import io
import pathlib
import urllib.request
import sys

from PIL import Image, ImageDraw, ImageFont, features
from fontTools.ttLib import TTFont


ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.dont_write_bytecode = True
REVISION = "8b0a1d0f5983c89bc2b93f1b5fb55f9e252744b5"
FONT_URL = (f"https://raw.githubusercontent.com/google/fonts/{REVISION}/"
            "ofl/notoemoji/NotoEmoji%5Bwght%5D.ttf")
FONT_SHA256 = "de6c18832938afc99caf132b39d6a30a19bac7f2e812e28db2535b4608d27551"

COLOR_URL = "https://raw.githubusercontent.com/googlefonts/noto-emoji/v2.051/fonts/NotoColorEmoji.ttf"
COLOR_SHA256 = "72a635cb3d2f3524c51620cdde406b217204e8a6a06c6a096ff8ed4b5fd6e27b"
VARIATION_URL = "https://www.unicode.org/Public/18.0.0/ucd/emoji/emoji-variation-sequences.txt"
VARIATION_SHA256 = "ff1707564aa1f1b2fcf4ec92d609d4cb26940bc0d4dcf07f5328ad6879e84da3"


def pinned_bytes(path, url, digest):
    if path is not None:
        data = path.read_bytes()
    else:
        with urllib.request.urlopen(url) as response:
            data = response.read()
    if hashlib.sha256(data).hexdigest() != digest:
        raise ValueError(f"Input does not match pinned SHA-256: {url}")
    return data


def variations(source_dir):
    path = None if source_dir is None else source_dir / "emoji-variation-sequences-18.0.0.txt"
    data = pinned_bytes(path, VARIATION_URL, VARIATION_SHA256)
    return {"".join(chr(int(cp, 16)) for cp in line.split(";")[0].split())
            for line in data.decode("utf-8").splitlines()
            if line.strip() and not line.startswith("#")}


def raster_size(source_width, source_height, width, height, tall_pixels=False):
    padding_x = 0 if width == 8 or height == 8 else 1
    padding_y = 0 if height == 8 else 1
    ratio = 6 / 5 if tall_pixels else 1
    # Fit in physical space, then project directly into destination pixels.
    scale = min((width - padding_x * 2) / source_width,
                (height - padding_y * 2) * ratio / source_height)
    return (max(1, round(source_width * scale)),
            max(1, round(source_height * scale / ratio)))


def color_raster(source, glyph, width, height, tall_pixels=False):
    name = source.getGlyphOrder()[glyph]
    image = Image.open(io.BytesIO(source["CBDT"].strikeData[0][name].imageData)).convert("RGBA")
    image = image.crop(image.getbbox())
    # Both projections are resampled from the original premultiplied source.
    image = image.convert("RGBa").resize(
        raster_size(image.width, image.height, width, height, tall_pixels),
        Image.Resampling.LANCZOS).convert("RGBA")
    canvas = Image.new("RGBA", (width, height))
    canvas.paste(image, ((width - image.width) // 2, (height - image.height) // 2))
    return canvas.tobytes()


def encode_color_rll(rgba, width, height):
    if len(rgba) != width * height * 4:
        raise ValueError("Unexpected colour raster size")
    encoded = bytearray(2)
    for half in range(2):
        first_x = 0 if half == 0 else width // 2
        span = width // 2 if half == 0 else width - width // 2
        if half:
            if len(encoded) > 0xffff:
                raise ValueError("RLL half offset exceeds 16 bits")
            encoded[:2] = len(encoded).to_bytes(2, "little")
        pixel, end = 0, span * height
        while pixel < end:
            source = ((pixel // span) * width + first_x + pixel % span) * 4
            if rgba[source + 3]:
                encoded.extend((rgba[source + 3], *rgba[source:source + 3]))
                pixel += 1
            else:
                run = 0
                while pixel < end and run < 256:
                    source = ((pixel // span) * width + first_x + pixel % span) * 4
                    if rgba[source + 3]:
                        break
                    pixel += 1
                    run += 1
                encoded.extend((0, run - 1))
    return bytes(encoded)


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


graphemes = module("emoji_graphemes", "src/text/unicode_grapheme/update_unicode_grapheme.py")
widths = module("emoji_widths", "src/text/unicode_width/update_unicode_width.py")
fonts = module("emoji_fonts", "src/media/fonts/update_fonts.py")


class Shaper:
    """Ask HarfBuzz whether a sequence resolves to one non-.notdef glyph."""

    class Info(ct.Structure):
        _fields_ = [(name, ct.c_uint32) for name in
                    ("codepoint", "mask", "cluster", "var1", "var2")]

    def __init__(self, data):
        path = ctypes.util.find_library("harfbuzz")
        if path is None:
            raise RuntimeError("HarfBuzz shared library not found")
        self.hb = ct.CDLL(path)
        declarations = {
            "hb_blob_create": (ct.c_void_p, [ct.c_char_p, ct.c_uint, ct.c_int,
                                           ct.c_void_p, ct.c_void_p]),
            "hb_blob_destroy": (None, [ct.c_void_p]),
            "hb_face_create": (ct.c_void_p, [ct.c_void_p, ct.c_uint]),
            "hb_face_destroy": (None, [ct.c_void_p]),
            "hb_font_create": (ct.c_void_p, [ct.c_void_p]),
            "hb_font_destroy": (None, [ct.c_void_p]),
            "hb_ot_font_set_funcs": (None, [ct.c_void_p]),
            "hb_buffer_create": (ct.c_void_p, []),
            "hb_buffer_destroy": (None, [ct.c_void_p]),
            "hb_buffer_add_utf8": (None, [ct.c_void_p, ct.c_char_p, ct.c_int,
                                         ct.c_uint, ct.c_int]),
            "hb_buffer_guess_segment_properties": (None, [ct.c_void_p]),
            "hb_buffer_set_flags": (None, [ct.c_void_p, ct.c_uint]),
            "hb_shape": (None, [ct.c_void_p, ct.c_void_p, ct.c_void_p, ct.c_uint]),
            "hb_buffer_get_glyph_infos": (ct.POINTER(self.Info),
                                          [ct.c_void_p, ct.POINTER(ct.c_uint)]),
        }
        for name, (result, arguments) in declarations.items():
            fn = getattr(self.hb, name)
            fn.restype, fn.argtypes = result, arguments
        blob = self.hb.hb_blob_create(data, len(data), 0, None, None)
        face = self.hb.hb_face_create(blob, 0)
        self.font = self.hb.hb_font_create(face)
        self.hb.hb_ot_font_set_funcs(self.font)
        self.hb.hb_face_destroy(face)
        self.hb.hb_blob_destroy(blob)

    def glyph(self, text):
        encoded = text.encode("utf-8")
        buffer = self.hb.hb_buffer_create()
        try:
            # Remove leftover invisible joiners/selectors after shaping,
            # rather than counting their zero-width space glyphs as art.
            self.hb.hb_buffer_set_flags(buffer, 8)
            self.hb.hb_buffer_add_utf8(buffer, encoded, len(encoded), 0, len(encoded))
            self.hb.hb_buffer_guess_segment_properties(buffer)
            self.hb.hb_shape(self.font, buffer, None, 0)
            count = ct.c_uint()
            info = self.hb.hb_buffer_get_glyph_infos(buffer, ct.byref(count))
            return info[0].codepoint if count.value == 1 and info[0].codepoint else None
        finally:
            self.hb.hb_buffer_destroy(buffer)

    def close(self):
        self.hb.hb_font_destroy(self.font)


def in_ranges(codepoint, ranges):
    return any(first <= codepoint <= last for first, last in ranges)


def repertoire(source_dir):
    data = graphemes.source_bytes("emoji", source_dir)
    emoji = widths.property_ranges(data, "emoji-data", {"Emoji"})
    presentation = widths.property_ranges(data, "emoji-data", {"Emoji_Presentation"})
    name, url, digest, _ = widths.SOURCES[0]
    wide = widths.property_ranges(widths.source_bytes(source_dir, name, url, digest),
                                 name, {"W", "F"}) + presentation
    entries = {}
    for first, last in emoji:
        for codepoint in range(first, last + 1):
            # ASCII keycap bases retain their normal text glyphs. Their
            # complete emoji sequences are included below.
            if codepoint > 0x7f:
                entries[chr(codepoint)] = 2 if in_ranges(codepoint, wide) else 1
    for name in ("sequences", "zwj"):
        for sequence in graphemes.emoji_sequences(graphemes.source_bytes(name, source_dir)):
            if len(sequence) > 1:
                entries["".join(map(chr, sequence))] = 2
    # VS15 explicitly requests text presentation; VS16 requests emoji.
    # Both must resolve as one cluster, including on BBS fonts with no
    # native emoji. Unqualified forms of RGI sequences share the artwork.
    for text, columns in list(entries.items()):
        if len(text) == 1:
            entries[text + "\ufe0e"] = 1 if not in_ranges(ord(text), wide) else 2
            entries[text + "\ufe0f"] = 2
        elif "\ufe0f" in text:
            entries.setdefault(text.replace("\ufe0f", ""), columns)
    # Keep historical permissive aliases monochrome, but admit every official
    # selector form, including ASCII keycap bases.
    for text in variations(source_dir):
        entries[text] = 2 if text.endswith("\ufe0f") or in_ranges(ord(text[0]), wide) else 1
    return entries


def raster(font, text, width, height):
    box = font.getbbox(text, direction="ltr")
    image = Image.new("L", (box[2] - box[0], box[3] - box[1]))
    ImageDraw.Draw(image).text((-box[0], -box[1]), text, font=font,
                              fill=255, direction="ltr")
    box = image.getbbox()
    if box is None:
        raise ValueError(f"Empty emoji bitmap: {text!r}")
    image = image.crop(box)
    padding_x = 0 if width == 8 or height == 8 else 1
    padding_y = 0 if height == 8 else 1
    scale = min((width - padding_x * 2) / image.width,
                (height - padding_y * 2) / image.height)
    image = image.resize((max(1, round(image.width * scale)),
                          max(1, round(image.height * scale))), Image.Resampling.LANCZOS)
    canvas = Image.new("L", (width, height))
    canvas.paste(image, ((width - image.width) // 2, (height - image.height) // 2))
    # A fixed threshold and bold source weight keep fine outlines visible
    # at 8 pixels without adding stochastic dithering to terminal glyphs.
    bitmap = canvas.point(lambda value: 255 if value >= 80 else 0, mode="1").tobytes()
    if not any(bitmap):
        raise ValueError(f"Emoji disappeared at {width}x{height}: {text!r}")
    return bitmap


def write_source(path, entries, font, color_source):
    with io.StringIO() as out:
        out.write("/* Generated by fonts/update_emoji.py; do not edit. */\n")
        out.write("/* SyncTERM Emoji: Noto Emoji 3.002 (weight 700), Noto Color Emoji 2.051. */\n")
        out.write("/* SPDX-License-Identifier: OFL-1.1 AND Unicode-3.0 */\n")
        out.write("/* See fonts/LICENSE.NOTO-EMOJI and fonts/UPSTREAM.md. */\n\n")
        out.write('#include "st_emoji_fonts.h"\n\n')
        # Share identical source glyphs and presentation widths. The
        # mappings remain separate so each sequence round-trips intact.
        keys = sorted(set((glyph, columns, color) for _, columns, glyph, color in entries),
                      key=lambda key: (key[1], key[0], -1 if key[2] is None else key[2]))
        ids = {key: index for index, key in enumerate(keys)}
        narrow = sum(columns == 1 for _, columns, _ in keys)
        representatives = {(glyph, columns): text for text, columns, glyph, _ in entries}
        for height in (8, 14, 16):
            mono = {(glyph, columns): raster(font, representatives[glyph, columns], columns * 8, height)
                    for glyph, columns in representatives}
            bitmaps = [mono[glyph, columns] for glyph, columns, _ in keys]
            fonts.byte_array(out, f"emoji_{height}_narrow", b"".join(bitmaps[:narrow]))
            fonts.byte_array(out, f"emoji_{height}_wide", b"".join(bitmaps[narrow:]))
            # Colour glyphs can share pixels even when their monochrome
            # source or presentation aliases differ.
            colors = sorted({(color, columns) for _, columns, color in keys if color is not None})
            payloads = {}
            names = {}
            for color, columns in colors:
                for tall in (False, True):
                    data = encode_color_rll(color_raster(color_source, color, columns * 8,
                                                        height, tall), columns * 8, height)
                    name = f"rll_{height}_{color}_{columns}" + ("_tall" if tall else "")
                    if data not in payloads:
                        payloads[data] = name
                        fonts.byte_array(out, name, data)
                    names[color, columns, tall] = payloads[data]
            out.write(f"static const struct st_console_color_glyph emoji_{height}_colors[] = {{\n")
            for index, (_, columns, color) in enumerate(keys):
                if color is not None:
                    name = names[color, columns, False]
                    tall = names[color, columns, True]
                    out.write(f"\t{{{index}, {name}, sizeof({name}), 0, 0, {tall}, sizeof({tall})}},\n")
            out.write("};\n\n")
        out.write("static const struct st_console_font_mapping emoji_mapping[] = {\n")
        for text, columns, glyph, color in entries:
            if len(text) == 1:
                out.write(f"\t{{UINT32_C(0x{ord(text):x}), {ids[glyph, columns, color]}}},\n")
        out.write("};\n\n")
        out.write("static const struct st_console_font_cluster_mapping emoji_clusters[] = {\n")
        for text, columns, glyph, color in entries:
            if len(text) > 1:
                encoded = text.encode("utf-8")
                literal = "".join(f"\\x{byte:02x}" for byte in encoded)
                out.write(f'\t{{"{literal}", {len(encoded)}, {ids[glyph, columns, color]}}},\n')
        out.write("};\n\n")
        for height in (8, 14, 16):
            out.write(f"const struct st_console_font st_emoji_{height}_font = {{\n")
            out.write(f"\t.bitmap = emoji_{height}_narrow,\n")
            out.write(f"\t.bitmap_size = sizeof(emoji_{height}_narrow),\n")
            out.write(f"\t.width = 8, .height = {height}, .glyph_count = {narrow},\n")
            out.write(f"\t.row_stride = 1, .glyph_stride = {height},\n")
            out.write(f"\t.wide_bitmap = emoji_{height}_wide,\n")
            out.write(f"\t.wide_bitmap_size = sizeof(emoji_{height}_wide),\n")
            out.write(f"\t.wide_glyph_count = {len(keys) - narrow},\n")
            out.write(f"\t.wide_row_stride = 2, .wide_glyph_stride = {height * 2},\n")
            out.write(f"\t.color_glyphs = emoji_{height}_colors,\n")
            out.write(f"\t.color_glyph_count = sizeof(emoji_{height}_colors) / sizeof(emoji_{height}_colors[0]),\n")
            out.write("\t.mapping = emoji_mapping,\n")
            out.write("\t.mapping_count = sizeof(emoji_mapping) / sizeof(emoji_mapping[0]),\n")
            out.write("\t.cluster_mapping = emoji_clusters,\n")
            out.write("\t.cluster_mapping_count = sizeof(emoji_clusters) / sizeof(emoji_clusters[0]),\n")
            out.write("};\n\n")
        path.write_text(out.getvalue().rstrip() + "\n", encoding="ascii", newline="\n")
    print(f"{len(entries)} emoji mappings; {len(keys)} glyph/presentation variants")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=pathlib.Path)
    parser.add_argument("--font", type=pathlib.Path,
                        help="use a local copy of the pinned Noto Emoji font")
    parser.add_argument("--color-font", type=pathlib.Path,
                        help="use a local copy of pinned Noto Color Emoji")
    parser.add_argument("--root", type=pathlib.Path, default=ROOT)
    args = parser.parse_args()
    if not features.check_feature("raqm"):
        raise RuntimeError("Pillow must be built with RAQM for emoji shaping")
    if args.font:
        data = args.font.read_bytes()
    else:
        with urllib.request.urlopen(FONT_URL) as response:
            data = response.read()
    if hashlib.sha256(data).hexdigest() != FONT_SHA256:
        raise ValueError("Noto Emoji font does not match the pinned SHA-256")
    font = ImageFont.truetype(io.BytesIO(data), 128, layout_engine=ImageFont.Layout.RAQM)
    font.set_variation_by_axes([700])
    color_data = pinned_bytes(args.color_font, COLOR_URL, COLOR_SHA256)
    color_source = TTFont(io.BytesIO(color_data))
    color_shaper = Shaper(color_data)
    official = variations(args.source_dir)
    defaults = widths.property_ranges(graphemes.source_bytes("emoji", args.source_dir),
                                      "emoji-data", {"Emoji_Presentation"})
    shaper = Shaper(data)
    try:
        entries = []
        for text, columns in sorted(repertoire(args.source_dir).items()):
            glyph = shaper.glyph(text)
            if glyph is not None:
                eligible = (in_ranges(ord(text), defaults) if len(text) == 1 else
                            text in official if text.endswith("\ufe0f") and len(text) == 2 else
                            "\ufe0e" not in text)
                color = color_shaper.glyph(text) if eligible else None
                if color is not None and color_source.getGlyphOrder()[color] not in color_source["CBDT"].strikeData[0]:
                    color = None
                entries.append((text, columns, glyph, color))
        path = args.root / "src/media/fonts/generated/st_emoji_data.c"
        path.parent.mkdir(parents=True, exist_ok=True)
        write_source(path, entries, font, color_source)
    finally:
        shaper.close()
        color_shaper.close()
        color_source.close()

    # Keep picker names, variants and actual glyph coverage in step.
    import runpy
    catalog_dir = args.root / "src/text/emoji"
    sys.path.insert(0, str(catalog_dir))
    try:
        runpy.run_path(str(catalog_dir / "update_emoji_catalog.py"))["refresh_repository"](args.root)
    finally:
        sys.path.pop(0)


if __name__ == "__main__":
    main()
