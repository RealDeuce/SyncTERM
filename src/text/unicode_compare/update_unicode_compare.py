#!/usr/bin/env python3
"""Fetch pinned Unicode 18 data and generate comparison tables."""

# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

import argparse
import hashlib
import pathlib
import re
import urllib.request


VERSION = "18.0.0"
SOURCES = {
    "casefold": (
        f"CaseFolding-{VERSION}.txt",
        f"https://www.unicode.org/Public/{VERSION}/ucd/CaseFolding.txt",
        "a004797658a457bec4dc11683e39f69249ea3b595b752dbea6721c4c9f587b0d",
    ),
    "collation": (
        f"allkeys-{VERSION}.txt",
        f"https://www.unicode.org/Public/{VERSION}/uca/allkeys.txt",
        "9b7539f54715e9b9f6686eee87fd846185e689eef7f182ac23fd5eae1eca5814",
    ),
    "unicode": (
        f"UnicodeData-{VERSION}.txt",
        f"https://www.unicode.org/Public/{VERSION}/ucd/UnicodeData.txt",
        "0736451de439ae7baf1425136617da495e09ee5afbe6e394374db7009ea08950",
    ),
    "properties": (
        f"PropList-{VERSION}.txt",
        f"https://www.unicode.org/Public/{VERSION}/ucd/PropList.txt",
        "f438f532e8737bb8a2702126cdf9c4af5e357c58c7acf9d9eb2fc7c1a1d955d6",
    ),
}


def source_bytes(name, source_dir):
    filename, url, expected = SOURCES[name]
    if source_dir is None:
        with urllib.request.urlopen(url) as response:
            data = response.read()
    else:
        data = (source_dir / filename).read_bytes()
    actual = hashlib.sha256(data).hexdigest()
    if actual != expected:
        raise ValueError(f"{filename}: expected SHA-256 {expected}, got {actual}")
    return data


def parse_casefold(data):
    result = {}
    for number, raw in enumerate(data.decode("utf-8-sig").splitlines(), 1):
        content = raw.split("#", 1)[0].strip()
        if not content:
            continue
        fields = [field.strip() for field in content.split(";")]
        if fields and fields[-1] == "":
            fields.pop()
        if len(fields) != 3:
            raise ValueError(f"CaseFolding:{number}: malformed row")
        codepoint = int(fields[0], 16)
        status = fields[1]
        if status not in ("C", "F"):
            continue
        values = tuple(int(value, 16) for value in fields[2].split())
        if len(values) > 3:
            raise ValueError(f"CaseFolding:{number}: mapping too long")
        result[codepoint] = values
    return result


CE_RE = re.compile(r"\[([.*])([0-9A-F]{4})\.([0-9A-F]{4})\.([0-9A-F]{4})\]")


def parse_collation(data):
    mappings = []
    implicit = []
    implicit_bases = {}
    for number, raw in enumerate(data.decode("utf-8-sig").splitlines(), 1):
        content = raw.split("#", 1)[0].strip()
        if not content:
            continue
        if content.startswith("@implicitweights"):
            match = re.fullmatch(
                r"@implicitweights\s+([0-9A-F]+)\.\.([0-9A-F]+);\s*([0-9A-F]+)",
                content)
            if match is None:
                raise ValueError(f"allkeys:{number}: malformed implicit range")
            first, last, lead = (int(value, 16) for value in match.groups())
            base = implicit_bases.setdefault(lead, first)
            implicit.append((first, last, lead, base))
            continue
        if content.startswith("@"):
            continue
        try:
            encoded, weights = content.split(";", 1)
            sequence = tuple(int(value, 16) for value in encoded.split())
        except ValueError as error:
            raise ValueError(f"allkeys:{number}: malformed mapping") from error
        elements = tuple((int(a, 16), int(b, 16), int(c, 16))
                         for _, a, b, c in CE_RE.findall(weights))
        if not sequence or len(sequence) > 3 or not elements:
            raise ValueError(f"allkeys:{number}: unsupported mapping")
        mappings.append((sequence, elements))
    return sorted(mappings), sorted(implicit)


PROPERTY_BITS = {
    "White_Space": 1 << 0,
    "Bidi_Control": 1 << 1,
    "Noncharacter_Code_Point": 1 << 2,
    "Unified_Ideograph": 1 << 3,
    "Variation_Selector": 1 << 9,
}

CATEGORY_BITS = {
    "Cc": 1 << 4,
    "Cn": 1 << 5,
    "Co": 1 << 6,
    "Cf": 1 << 7,
    "Zl": 1 << 8,
    "Zp": 1 << 8,
}


def parse_unicode_categories(data):
    categories = ["Cn"] * 0x110000
    pending = None
    for number, raw in enumerate(data.decode("utf-8").splitlines(), 1):
        fields = raw.split(";")
        if len(fields) != 15:
            raise ValueError(f"UnicodeData:{number}: malformed row")
        codepoint = int(fields[0], 16)
        name = fields[1]
        category = fields[2]
        if name.endswith(", First>"):
            pending = (codepoint, category)
        elif name.endswith(", Last>"):
            if pending is None or pending[1] != category:
                raise ValueError(f"UnicodeData:{number}: unmatched range")
            for value in range(pending[0], codepoint + 1):
                categories[value] = category
            pending = None
        else:
            categories[codepoint] = category
    if pending is not None:
        raise ValueError("UnicodeData: unterminated range")
    return categories


def add_properties(flags, data):
    for number, raw in enumerate(data.decode("utf-8-sig").splitlines(), 1):
        content = raw.split("#", 1)[0].strip()
        if not content:
            continue
        try:
            encoded, prop = (field.strip() for field in content.split(";", 1))
        except ValueError as error:
            raise ValueError(f"PropList:{number}: malformed row") from error
        bit = PROPERTY_BITS.get(prop)
        if bit is None:
            continue
        if ".." in encoded:
            first, last = (int(value, 16) for value in encoded.split(".."))
        else:
            first = last = int(encoded, 16)
        for codepoint in range(first, last + 1):
            flags[codepoint] |= bit


def property_ranges(unicode_data, prop_data):
    categories = parse_unicode_categories(unicode_data)
    flags = [CATEGORY_BITS.get(category, 0) for category in categories]
    add_properties(flags, prop_data)
    ranges = []
    first = 0
    value = flags[0]
    for codepoint in range(1, len(flags) + 1):
        current = flags[codepoint] if codepoint < len(flags) else None
        if current != value:
            if value:
                ranges.append((first, codepoint - 1, value))
            first = codepoint
            value = current
    return ranges


def write_source(path, folds, mappings, implicit, properties):
    fold_values = []
    fold_rows = []
    for codepoint, values in sorted(folds.items()):
        offset = len(fold_values)
        fold_values.extend(values)
        fold_rows.append((codepoint, offset, len(values)))

    ces = []
    rows = []
    for sequence, elements in mappings:
        offset = len(ces)
        ces.extend(elements)
        padded = sequence + (0,) * (3 - len(sequence))
        rows.append((*padded, len(sequence), offset, len(elements)))

    with path.open("w", encoding="ascii", newline="\n") as out:
        out.write("/* Generated by unicode_compare/update_unicode_compare.py; do not edit. */\n")
        out.write(f"/* Unicode {VERSION}; see unicode_compare/UPSTREAM.md. */\n\n")
        out.write("/*\n * Copyright (c) 2026 Stephen Hurd\n")
        out.write(" * SPDX-License-Identifier: BSD-2-Clause AND Unicode-3.0\n */\n\n")
        out.write('#include "st_unicode_compare_data.h"\n\n')
        out.write("#include <string.h>\n\n")
        out.write("struct fold_row { uint32_t codepoint; uint32_t offset; uint8_t count; };\n")
        out.write("struct collation_row { uint32_t cp[3]; uint8_t count; uint32_t offset; uint8_t element_count; };\n")
        out.write("struct implicit_row { uint32_t first; uint32_t last; uint16_t lead; uint32_t base; };\n")
        out.write("struct property_row { uint32_t first; uint32_t last; uint32_t flags; };\n\n")

        out.write("static const uint32_t fold_values[] = {\n")
        for offset in range(0, len(fold_values), 8):
            out.write("\t" + ", ".join(
                f"UINT32_C(0x{value:x})" for value in fold_values[offset:offset + 8]) + ",\n")
        out.write("};\n\nstatic const struct fold_row folds[] = {\n")
        for codepoint, offset, count in fold_rows:
            out.write(f"\t{{UINT32_C(0x{codepoint:x}), {offset}, {count}}},\n")
        out.write("};\n\n")

        out.write("static const struct st_unicode_collation_element elements[] = {\n")
        for primary, secondary, tertiary in ces:
            out.write(f"\t{{UINT16_C(0x{primary:04x}), UINT16_C(0x{secondary:04x}), UINT16_C(0x{tertiary:04x})}},\n")
        out.write("};\n\nstatic const struct collation_row mappings[] = {\n")
        for a, b, c, count, offset, element_count in rows:
            out.write(f"\t{{{{UINT32_C(0x{a:x}), UINT32_C(0x{b:x}), UINT32_C(0x{c:x})}}, {count}, {offset}, {element_count}}},\n")
        out.write("};\n\nstatic const struct implicit_row implicit_ranges[] = {\n")
        for first, last, lead, base in implicit:
            out.write(f"\t{{UINT32_C(0x{first:x}), UINT32_C(0x{last:x}), UINT16_C(0x{lead:04x}), UINT32_C(0x{base:x})}},\n")
        out.write("};\n\nstatic const struct property_row property_ranges[] = {\n")
        for first, last, flags in properties:
            out.write(f"\t{{UINT32_C(0x{first:x}), UINT32_C(0x{last:x}), UINT32_C(0x{flags:x})}},\n")
        out.write("};\n\n")

        out.write(r'''static int
sequence_compare(const uint32_t *sequence, size_t count,
	const struct collation_row *row)
{
	size_t shared = count < row->count ? count : row->count;

	for (size_t i = 0; i < shared; ++i) {
		if (sequence[i] != row->cp[i])
			return sequence[i] < row->cp[i] ? -1 : 1;
	}
	return count == row->count ? 0 : count < row->count ? -1 : 1;
}

size_t
st_unicode_casefold_mapping(uint32_t codepoint, uint32_t output[3])
{
	size_t left = 0;
	size_t right = sizeof(folds) / sizeof(folds[0]);

	while (left < right) {
		size_t middle = left + (right - left) / 2;

		if (folds[middle].codepoint < codepoint)
			left = middle + 1;
		else
			right = middle;
	}
	if (left == sizeof(folds) / sizeof(folds[0])
	    || folds[left].codepoint != codepoint) {
		output[0] = codepoint;
		return 1;
	}
	memcpy(output, fold_values + folds[left].offset,
	    folds[left].count * sizeof(*output));
	return folds[left].count;
}

size_t
st_unicode_collation_mapping(const uint32_t *sequence, size_t count,
	const struct st_unicode_collation_element **result)
{
	size_t left = 0;
	size_t right = sizeof(mappings) / sizeof(mappings[0]);

	while (left < right) {
		size_t middle = left + (right - left) / 2;
		int compared = sequence_compare(sequence, count, &mappings[middle]);

		if (compared > 0)
			left = middle + 1;
		else
			right = middle;
	}
	if (left == sizeof(mappings) / sizeof(mappings[0])
	    || sequence_compare(sequence, count, &mappings[left]) != 0)
		return 0;
	*result = elements + mappings[left].offset;
	return mappings[left].element_count;
}

bool
st_unicode_collation_has_prefix(const uint32_t *sequence, size_t count)
{
	size_t left = 0;
	size_t right = sizeof(mappings) / sizeof(mappings[0]);

	while (left < right) {
		size_t middle = left + (right - left) / 2;
		int compared = 0;

		for (size_t i = 0; i < count; ++i) {
			if (mappings[middle].cp[i] != sequence[i]) {
				compared = mappings[middle].cp[i] < sequence[i] ? -1 : 1;
				break;
			}
		}
		if (compared < 0)
			left = middle + 1;
		else
			right = middle;
	}
	return left < sizeof(mappings) / sizeof(mappings[0])
	    && mappings[left].count >= count
	    && memcmp(mappings[left].cp, sequence,
	    count * sizeof(*sequence)) == 0;
}

bool
st_unicode_implicit_range(uint32_t codepoint, uint16_t *lead, uint32_t *base)
{
	for (size_t i = 0;
	    i < sizeof(implicit_ranges) / sizeof(implicit_ranges[0]); ++i) {
		if (codepoint >= implicit_ranges[i].first
		    && codepoint <= implicit_ranges[i].last) {
			*lead = implicit_ranges[i].lead;
			*base = implicit_ranges[i].base;
			return true;
		}
	}
	return false;
}

uint32_t
st_unicode_property_flags(uint32_t codepoint)
{
	size_t left = 0;
	size_t right = sizeof(property_ranges) / sizeof(property_ranges[0]);

	while (left < right) {
		size_t middle = left + (right - left) / 2;

		if (property_ranges[middle].last < codepoint)
			left = middle + 1;
		else
			right = middle;
	}
	return left < sizeof(property_ranges) / sizeof(property_ranges[0])
	    && property_ranges[left].first <= codepoint
	    ? property_ranges[left].flags : 0;
}
''')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path,
                        default=pathlib.Path(__file__).parent /
                        "generated/st_unicode_compare_data.c")
    args = parser.parse_args()
    folds = parse_casefold(source_bytes("casefold", args.source_dir))
    mappings, implicit = parse_collation(
        source_bytes("collation", args.source_dir))
    properties = property_ranges(source_bytes("unicode", args.source_dir),
                                 source_bytes("properties", args.source_dir))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    write_source(args.output, folds, mappings, implicit, properties)


if __name__ == "__main__":
    main()
