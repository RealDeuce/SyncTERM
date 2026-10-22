#!/usr/bin/env python3
"""Generate monochrome emoji for SyncTERM's 8-pixel cell fonts.

Regeneration needs Pillow with RAQM and the HarfBuzz shared library. Normal
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


ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
REVISION = "8b0a1d0f5983c89bc2b93f1b5fb55f9e252744b5"
FONT_URL = (f"https://raw.githubusercontent.com/google/fonts/{REVISION}/"
            "ofl/notoemoji/NotoEmoji%5Bwght%5D.ttf")
FONT_SHA256 = "de6c18832938afc99caf132b39d6a30a19bac7f2e812e28db2535b4608d27551"


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


graphemes = module("emoji_graphemes", "unicode_grapheme/update_unicode_grapheme.py")
widths = module("emoji_widths", "unicode_width/update_unicode_width.py")
fonts = module("emoji_fonts", "fonts/update_fonts.py")


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
    # native emoji. Unqualified forms of RGI sequences share that glyph.
    for text, columns in list(entries.items()):
        if len(text) == 1:
            entries[text + "\ufe0e"] = 1 if not in_ranges(ord(text), wide) else 2
            entries[text + "\ufe0f"] = 2
        elif "\ufe0f" in text:
            entries.setdefault(text.replace("\ufe0f", ""), columns)
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


def write_source(path, entries, font):
    with io.StringIO() as out:
        out.write("/* Generated by fonts/update_emoji.py; do not edit. */\n")
        out.write("/* SyncTERM Emoji: Noto Emoji 3.002, rasterized at weight 700. */\n")
        out.write("/* SPDX-License-Identifier: OFL-1.1 AND Unicode-3.0 */\n")
        out.write("/* See fonts/LICENSE.NOTO-EMOJI and fonts/UPSTREAM.md. */\n\n")
        out.write('#include "st_emoji_fonts.h"\n\n')
        # Share identical source glyphs and presentation widths. The
        # mappings remain separate so each sequence round-trips intact.
        keys = sorted(set((glyph, columns) for _, columns, glyph in entries),
                      key=lambda key: (key[1], key[0]))
        ids = {key: index for index, key in enumerate(keys)}
        narrow = sum(columns == 1 for _, columns in keys)
        representatives = {(glyph, columns): text for text, columns, glyph in entries}
        for height in (8, 14, 16):
            bitmaps = [raster(font, representatives[key], key[1] * 8, height)
                       for key in keys]
            fonts.byte_array(out, f"emoji_{height}_narrow", b"".join(bitmaps[:narrow]))
            fonts.byte_array(out, f"emoji_{height}_wide", b"".join(bitmaps[narrow:]))
        out.write("static const struct st_console_font_mapping emoji_mapping[] = {\n")
        for text, columns, glyph in entries:
            if len(text) == 1:
                out.write(f"\t{{UINT32_C(0x{ord(text):x}), {ids[glyph, columns]}}},\n")
        out.write("};\n\n")
        out.write("static const struct st_console_font_cluster_mapping emoji_clusters[] = {\n")
        for text, columns, glyph in entries:
            if len(text) > 1:
                encoded = text.encode("utf-8")
                literal = "".join(f"\\x{byte:02x}" for byte in encoded)
                out.write(f'\t{{"{literal}", {len(encoded)}, {ids[glyph, columns]}}},\n')
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
            out.write("\t.mapping = emoji_mapping,\n")
            out.write("\t.mapping_count = sizeof(emoji_mapping) / sizeof(emoji_mapping[0]),\n")
            out.write("\t.cluster_mapping = emoji_clusters,\n")
            out.write("\t.cluster_mapping_count = sizeof(emoji_clusters) / sizeof(emoji_clusters[0]),\n")
            out.write("};\n\n")
        path.write_text(out.getvalue().rstrip() + "\n", encoding="ascii", newline="\n")
    print(f"{len(entries)} emoji mappings; {len(keys)} source glyph/width pairs")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=pathlib.Path)
    parser.add_argument("--font", type=pathlib.Path,
                        help="use a local copy of the pinned Noto Emoji font")
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
    shaper = Shaper(data)
    try:
        entries = []
        for text, columns in sorted(repertoire(args.source_dir).items()):
            glyph = shaper.glyph(text)
            if glyph is not None:
                entries.append((text, columns, glyph))
        path = args.root / "fonts/generated/st_emoji_data.c"
        path.parent.mkdir(parents=True, exist_ok=True)
        write_source(path, entries, font)
    finally:
        shaper.close()


if __name__ == "__main__":
    main()
