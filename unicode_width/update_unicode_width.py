#!/usr/bin/env python3
"""Fetch pinned Unicode data and regenerate SyncTERM's width table."""

# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

import argparse
import hashlib
import pathlib
import urllib.request


VERSION = "18.0.0"
SOURCES = (
    (
        f"EastAsianWidth-{VERSION}.txt",
        f"https://www.unicode.org/Public/{VERSION}/ucd/EastAsianWidth.txt",
        "a0cf29eacd00cfcaec4381c6b7c281685f18dbb4e7ff82b4076ccb342ca839aa",
        frozenset(("W", "F")),
    ),
    (
        f"emoji-data-{VERSION}.txt",
        f"https://www.unicode.org/Public/{VERSION}/ucd/emoji/emoji-data.txt",
        "80d00f8e616a0ef27fd6b8de3b758c06383b5d917e2977709578e68baf733bf1",
        frozenset(("Emoji_Presentation",)),
    ),
)

CORE = (f"DerivedCoreProperties-{VERSION}.txt",
        f"https://www.unicode.org/Public/{VERSION}/ucd/DerivedCoreProperties.txt",
        "09c928886a178fcafd93c29e4bd59073a058e5a100b716d425cb563ab50f68c9")
DATA = (f"UnicodeData-{VERSION}.txt",
        f"https://www.unicode.org/Public/{VERSION}/ucd/UnicodeData.txt",
        "0736451de439ae7baf1425136617da495e09ee5afbe6e394374db7009ea08950")


def source_bytes(source_dir, filename, url, sha256):
    if source_dir is None:
        with urllib.request.urlopen(url) as response:
            data = response.read()
    else:
        data = (source_dir / filename).read_bytes()
    actual = hashlib.sha256(data).hexdigest()
    if actual != sha256:
        raise ValueError(f"{filename}: expected SHA-256 {sha256}, got {actual}")
    return data


def property_ranges(data, filename, properties):
    ranges = []
    for line_number, raw in enumerate(data.decode("utf-8-sig").splitlines(), 1):
        content = raw.split("#", 1)[0].strip()
        if not content:
            continue
        try:
            encoded, prop = (part.strip() for part in content.split(";", 1))
            if prop not in properties:
                continue
            if ".." in encoded:
                first, last = (int(value, 16) for value in encoded.split(".."))
            else:
                first = last = int(encoded, 16)
        except ValueError as error:
            raise ValueError(f"{filename}:{line_number}: malformed line") from error
        if first > last or last > 0x10FFFF:
            raise ValueError(f"{filename}:{line_number}: invalid range")
        ranges.append((first, last))
    return ranges


def named_ranges(data, filename, properties):
    result = []
    for line_number, raw in enumerate(data.decode("utf-8-sig").splitlines(), 1):
        content = raw.split("#", 1)[0].strip()
        if not content:
            continue
        try:
            encoded, prop = (part.strip() for part in content.split(";", 1))
            if prop not in properties:
                continue
            if ".." in encoded:
                first, last = (int(value, 16) for value in encoded.split(".."))
            else:
                first = last = int(encoded, 16)
        except ValueError as error:
            raise ValueError(f"{filename}:{line_number}: malformed line") from error
        if first > last or last > 0x10FFFF:
            raise ValueError(f"{filename}:{line_number}: invalid range")
        result.append((first, last, prop))
    return result


def merge_ranges(ranges):
    result = []
    for first, last in sorted(ranges):
        if result and first <= result[-1][1] + 1:
            result[-1] = (result[-1][0], max(last, result[-1][1]))
        else:
            result.append((first, last))
    return result


def source_ranges(source_dir):
    eaw_name, eaw_url, eaw_hash, _ = SOURCES[0]
    emoji_name, emoji_url, emoji_hash, emoji_properties = SOURCES[1]
    eaw_data = source_bytes(source_dir, eaw_name, eaw_url, eaw_hash)
    emoji_data = source_bytes(source_dir, emoji_name, emoji_url, emoji_hash)
    eaw = named_ranges(eaw_data, eaw_name,
                       frozenset(("N", "A", "H", "Na", "W", "F")))
    emoji = merge_ranges(property_ranges(emoji_data, emoji_name,
                                         emoji_properties))
    wide = merge_ranges([(first, last) for first, last, prop in eaw
                         if prop in ("W", "F")] + emoji)
    ignorable = merge_ranges(property_ranges(source_bytes(source_dir, *CORE),
                            CORE[0], {"Default_Ignorable_Code_Point"}))
    marks = []
    zero = list(ignorable)
    first = None
    for line in source_bytes(source_dir, *DATA).decode("utf-8").splitlines():
        fields = line.split(";")
        codepoint = int(fields[0], 16)
        if fields[1].endswith(", First>"):
            first = codepoint
            continue
        start = first if first is not None else codepoint
        first = None
        if fields[2] in ("Mn", "Me"):
            marks.append((start, codepoint))
        if fields[2] in ("Mn", "Me", "Cf", "Cc"):
            zero.append((start, codepoint))
    return eaw, emoji, wide, ignorable, merge_ranges(marks), merge_ranges(zero)


def write_source(path, eaw, emoji, wide, ignorable, marks, zero):
    with path.open("w", encoding="ascii", newline="\n") as out:
        out.write("/* Generated by unicode_width/update_unicode_width.py; do not edit. */\n")
        out.write(f"/* Unicode {VERSION} width and emoji data; see unicode_width/UPSTREAM.md. */\n\n")
        out.write("/*\n * Copyright (c) 2026 Stephen Hurd\n")
        out.write(" * SPDX-License-Identifier: BSD-2-Clause AND Unicode-3.0\n */\n\n")
        out.write('#include "st_unicode_width.h"\n\n#include <stddef.h>\n\n')
        out.write("struct width_range {\n\tuint32_t first;\n\tuint32_t last;\n};\n\n")
        out.write("struct eaw_range {\n\tuint32_t first;\n\tuint32_t last;\n\tenum st_unicode_east_asian_width value;\n};\n\n")
        values = {"N": "NEUTRAL", "A": "AMBIGUOUS", "H": "HALFWIDTH",
                  "Na": "NARROW", "W": "WIDE", "F": "FULLWIDTH"}
        out.write("static const struct eaw_range eaw_ranges[] = {\n")
        for first, last, prop in eaw:
            out.write(f"\t{{UINT32_C(0x{first:x}), UINT32_C(0x{last:x}), "
                      f"ST_UNICODE_EAW_{values[prop]}}},\n")
        out.write("};\n\n")
        out.write("static const struct width_range emoji_presentation_ranges[] = {\n")
        for first, last in emoji:
            out.write(f"\t{{UINT32_C(0x{first:x}), UINT32_C(0x{last:x})}},\n")
        out.write("};\n\n")
        out.write("static const struct width_range wide_ranges[] = {\n")
        for first, last in wide:
            out.write(f"\t{{UINT32_C(0x{first:x}), UINT32_C(0x{last:x})}},\n")
        out.write("};\n\n")
        for name, ranges in (("ignorable", ignorable), ("mark", marks),
                             ("zero", zero)):
            out.write(f"static const struct width_range {name}_ranges[] = {{\n")
            for first, last in ranges:
                out.write(f"\t{{UINT32_C(0x{first:x}), UINT32_C(0x{last:x})}},\n")
            out.write("};\n\n")
        out.write(r'''static bool
scalar(uint32_t codepoint)
{
	return codepoint <= UINT32_C(0x10ffff)
	    && !(codepoint >= UINT32_C(0xd800) && codepoint <= UINT32_C(0xdfff));
}

enum st_unicode_east_asian_width
st_unicode_east_asian_width(uint32_t codepoint)
{
	size_t left = 0;
	size_t right = sizeof(eaw_ranges) / sizeof(eaw_ranges[0]);

	if (!scalar(codepoint))
		return ST_UNICODE_EAW_INVALID;
	while (left < right) {
		size_t middle = left + (right - left) / 2;

		if (eaw_ranges[middle].last < codepoint)
			left = middle + 1;
		else
			right = middle;
	}
	return left < sizeof(eaw_ranges) / sizeof(eaw_ranges[0])
	    && eaw_ranges[left].first <= codepoint ? eaw_ranges[left].value
	    : ST_UNICODE_EAW_NEUTRAL;
}

bool
st_unicode_emoji_presentation(uint32_t codepoint)
{
	size_t left = 0;
	size_t right = sizeof(emoji_presentation_ranges)
	    / sizeof(emoji_presentation_ranges[0]);

	if (!scalar(codepoint))
		return false;
	while (left < right) {
		size_t middle = left + (right - left) / 2;

		if (emoji_presentation_ranges[middle].last < codepoint)
			left = middle + 1;
		else
			right = middle;
	}
	return left < sizeof(emoji_presentation_ranges)
	    / sizeof(emoji_presentation_ranges[0])
	    && emoji_presentation_ranges[left].first <= codepoint;
}

''')
        for function, name in (("st_unicode_default_ignorable", "ignorable"),
                               ("st_unicode_nonspacing_mark", "mark"),
                               ("zero_width", "zero")):
            out.write(f'''{'static ' if function == 'zero_width' else ''}bool
{function}(uint32_t codepoint)
{{
\tsize_t left = 0;
\tsize_t right = sizeof({name}_ranges) / sizeof({name}_ranges[0]);

\twhile (left < right) {{
\t\tsize_t middle = left + (right - left) / 2;
\t\tif ({name}_ranges[middle].last < codepoint)
\t\t\tleft = middle + 1;
\t\telse
\t\t\tright = middle;
\t}}
\treturn left < sizeof({name}_ranges) / sizeof({name}_ranges[0])
\t    && {name}_ranges[left].first <= codepoint;
}}

''')
        out.write("uint8_t\nst_unicode_columns(uint32_t codepoint)\n{\n")
        out.write("\tsize_t left = 0;\n")
        out.write("\tsize_t right = sizeof(wide_ranges) / sizeof(wide_ranges[0]);\n\n")
        out.write("\tif (!scalar(codepoint) || zero_width(codepoint))\n\t\treturn 0;\n")
        out.write("\twhile (left < right) {\n")
        out.write("\t\tsize_t middle = left + (right - left) / 2;\n\n")
        out.write("\t\tif (wide_ranges[middle].last < codepoint)\n")
        out.write("\t\t\tleft = middle + 1;\n\t\telse\n\t\t\tright = middle;\n\t}\n")
        out.write("\treturn left < sizeof(wide_ranges) / sizeof(wide_ranges[0])\n")
        out.write("\t    && wide_ranges[left].first <= codepoint ? 2 : 1;\n}\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", type=pathlib.Path)
    parser.add_argument("--root", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    output = args.root / "unicode_width" / "generated"
    output.mkdir(parents=True, exist_ok=True)
    write_source(output / "st_unicode_width.c", *source_ranges(args.source_dir))


if __name__ == "__main__":
    main()
