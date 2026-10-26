#!/usr/bin/env python3
"""Fetch pinned Unicode data and generate grapheme and emoji oracles."""

# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

import argparse
import hashlib
import pathlib
import urllib.request


VERSION = "18.0.0"
SOURCES = {
    "grapheme": (
        f"GraphemeBreakProperty-{VERSION}.txt",
        f"https://www.unicode.org/Public/{VERSION}/ucd/auxiliary/GraphemeBreakProperty.txt",
        "0839dcb79e4ac639ecd538b1abf7c9d22e3f9dd265b7e182d33627aa4d75b45a",
    ),
    "core": (
        f"DerivedCoreProperties-{VERSION}.txt",
        f"https://www.unicode.org/Public/{VERSION}/ucd/DerivedCoreProperties.txt",
        "09c928886a178fcafd93c29e4bd59073a058e5a100b716d425cb563ab50f68c9",
    ),
    "emoji": (
        f"emoji-data-{VERSION}.txt",
        f"https://www.unicode.org/Public/{VERSION}/ucd/emoji/emoji-data.txt",
        "80d00f8e616a0ef27fd6b8de3b758c06383b5d917e2977709578e68baf733bf1",
    ),
    "sequences": (
        f"emoji-sequences-{VERSION}.txt",
        f"https://www.unicode.org/Public/{VERSION}/emoji/emoji-sequences.txt",
        "1823dce71f3dd9cb0ad1976797baee4ffbdc1909756f43af8ce4c57f732843a4",
    ),
    "zwj": (
        f"emoji-zwj-sequences-{VERSION}.txt",
        f"https://www.unicode.org/Public/{VERSION}/emoji/emoji-zwj-sequences.txt",
        "f61b5213bdf85a57741a9f903cb38d0f39a3d13be6ff4aca3b39857e859a9191",
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


def encoded_range(encoded):
    if ".." in encoded:
        first, last = (int(value, 16) for value in encoded.split(".."))
    else:
        first = last = int(encoded, 16)
    if first > last or last > 0x10FFFF:
        raise ValueError("invalid Unicode range")
    return first, last


def property_ranges(data, wanted, namespace=None):
    result = []
    for line_number, raw in enumerate(data.decode("utf-8-sig").splitlines(), 1):
        content = raw.split("#", 1)[0].strip()
        if not content:
            continue
        fields = [part.strip() for part in content.split(";")]
        if len(fields) < 2:
            raise ValueError(f"line {line_number}: too few fields")
        if namespace is None:
            prop = fields[1]
        else:
            if fields[1] != namespace:
                continue
            if len(fields) < 3:
                raise ValueError(f"line {line_number}: too few fields")
            prop = fields[2]
        if prop not in wanted:
            continue
        first, last = encoded_range(fields[0])
        result.append((first, last, prop))
    return sorted(result)


def emoji_sequences(data):
    result = set()
    for line_number, raw in enumerate(data.decode("utf-8-sig").splitlines(), 1):
        content = raw.split("#", 1)[0].strip()
        if not content:
            continue
        encoded = content.split(";", 1)[0].strip()
        parts = encoded.split()
        try:
            if len(parts) == 1 and ".." in parts[0]:
                first, last = encoded_range(parts[0])
                result.update((codepoint,) for codepoint in range(first, last + 1))
            else:
                result.add(tuple(int(value, 16) for value in parts))
        except ValueError as error:
            raise ValueError(f"emoji sequence line {line_number}: malformed") from error
    return result


class TrieNode:
    def __init__(self):
        self.children = {}
        self.terminal = False
        self.index = None


def emoji_trie(sequences):
    root = TrieNode()
    for sequence in sequences:
        node = root
        for codepoint in sequence:
            node = node.children.setdefault(codepoint, TrieNode())
        node.terminal = True
    nodes = []

    def assign(node):
        node.index = len(nodes)
        nodes.append(node)
        for codepoint in sorted(node.children):
            assign(node.children[codepoint])

    assign(root)
    edges = []
    rows = []
    for node in nodes:
        offset = len(edges)
        edges.extend((codepoint, child.index)
                     for codepoint, child in sorted(node.children.items()))
        rows.append((offset, len(node.children), node.terminal))
    return rows, edges


def write_ranges(out, name, rows, enum_prefix=None):
    if enum_prefix is None:
        out.write(f"static const struct codepoint_range {name}[] = {{\n")
        for first, last, _ in rows:
            out.write(f"\t{{UINT32_C(0x{first:x}), UINT32_C(0x{last:x})}},\n")
    else:
        out.write(f"static const struct property_range {name}[] = {{\n")
        for first, last, prop in rows:
            out.write(f"\t{{UINT32_C(0x{first:x}), UINT32_C(0x{last:x}), "
                      f"{enum_prefix}{prop.upper()}}},\n")
    out.write("};\n\n")


def write_source(path, grapheme, incb, pictographic, sequences):
    nodes, edges = emoji_trie(sequences)
    with path.open("w", encoding="ascii", newline="\n") as out:
        out.write("/* Generated by unicode_grapheme/update_unicode_grapheme.py; do not edit. */\n")
        out.write(f"/* Unicode {VERSION}; see unicode_grapheme/UPSTREAM.md. */\n\n")
        out.write("/*\n * Copyright (c) 2026 Stephen Hurd\n")
        out.write(" * SPDX-License-Identifier: BSD-2-Clause AND Unicode-3.0\n */\n\n")
        out.write('#include "st_unicode_grapheme.h"\n\n')
        out.write("struct property_range { uint32_t first; uint32_t last; uint8_t value; };\n")
        out.write("struct codepoint_range { uint32_t first; uint32_t last; };\n")
        out.write("struct trie_node { uint32_t edge; uint16_t count; bool terminal; };\n")
        out.write("struct trie_edge { uint32_t codepoint; uint32_t child; };\n\n")
        gnames = {
            "Regional_Indicator": "REGIONAL_INDICATOR",
            "SpacingMark": "SPACING_MARK",
        }
        normalized = [(a, b, gnames.get(prop, prop))
                      for a, b, prop in grapheme]
        write_ranges(out, "grapheme_ranges", normalized,
                     "ST_UNICODE_GRAPHEME_")
        write_ranges(out, "incb_ranges", incb, "ST_UNICODE_INCB_")
        write_ranges(out, "extended_pictographic_ranges", pictographic)
        out.write("static const struct trie_node emoji_nodes[] = {\n")
        for edge, count, terminal in nodes:
            out.write(f"\t{{{edge}, {count}, {'true' if terminal else 'false'}}},\n")
        out.write("};\n\nstatic const struct trie_edge emoji_edges[] = {\n")
        for codepoint, child in edges:
            out.write(f"\t{{UINT32_C(0x{codepoint:x}), {child}}},\n")
        out.write("};\n\n")
        out.write(r'''static uint8_t
property(uint32_t codepoint, const struct property_range *ranges,
	size_t count)
{
	size_t left = 0;
	size_t right = count;

	while (left < right) {
		size_t middle = left + (right - left) / 2;

		if (ranges[middle].last < codepoint)
			left = middle + 1;
		else
			right = middle;
	}
	return left < count && ranges[left].first <= codepoint
	    ? ranges[left].value : 0;
}

enum st_unicode_grapheme_break
st_unicode_grapheme_break(uint32_t codepoint)
{
	return (enum st_unicode_grapheme_break)property(codepoint,
	    grapheme_ranges, sizeof(grapheme_ranges) / sizeof(grapheme_ranges[0]));
}

enum st_unicode_indic_conjunct_break
st_unicode_indic_conjunct_break(uint32_t codepoint)
{
	return (enum st_unicode_indic_conjunct_break)property(codepoint,
	    incb_ranges, sizeof(incb_ranges) / sizeof(incb_ranges[0]));
}

bool
st_unicode_extended_pictographic(uint32_t codepoint)
{
	size_t left = 0;
	size_t right = sizeof(extended_pictographic_ranges)
	    / sizeof(extended_pictographic_ranges[0]);

	while (left < right) {
		size_t middle = left + (right - left) / 2;

		if (extended_pictographic_ranges[middle].last < codepoint)
			left = middle + 1;
		else
			right = middle;
	}
	return left < sizeof(extended_pictographic_ranges)
	    / sizeof(extended_pictographic_ranges[0])
	    && extended_pictographic_ranges[left].first <= codepoint;
}

size_t
st_unicode_rgi_emoji_prefix(const uint32_t *codepoints, size_t count)
{
	uint32_t node = 0;
	size_t longest = 0;

	if (codepoints == NULL)
		return 0;
	for (size_t i = 0; i < count; ++i) {
		const struct trie_node *current = &emoji_nodes[node];
		size_t left = current->edge;
		size_t right = left + current->count;

		while (left < right) {
			size_t middle = left + (right - left) / 2;

			if (emoji_edges[middle].codepoint < codepoints[i])
				left = middle + 1;
			else
				right = middle;
		}
		if (left == current->edge + current->count
		    || emoji_edges[left].codepoint != codepoints[i])
			break;
		node = emoji_edges[left].child;
		if (emoji_nodes[node].terminal)
			longest = i + 1;
	}
	return longest;
}
''')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-dir", type=pathlib.Path)
    parser.add_argument("--root", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parents[3])
    args = parser.parse_args()
    grapheme_names = frozenset(("CR", "LF", "Control", "Extend", "ZWJ",
                                "Regional_Indicator", "Prepend", "SpacingMark",
                                "L", "V", "T", "LV", "LVT"))
    grapheme = property_ranges(source_bytes("grapheme", args.source_dir),
                               grapheme_names)
    incb = property_ranges(source_bytes("core", args.source_dir),
                           frozenset(("Consonant", "Extend", "Linker")),
                           "InCB")
    pictographic = property_ranges(source_bytes("emoji", args.source_dir),
                                   frozenset(("Extended_Pictographic",)))
    sequences = emoji_sequences(source_bytes("sequences", args.source_dir))
    sequences.update(emoji_sequences(source_bytes("zwj", args.source_dir)))
    output = args.root / "src/text/unicode_grapheme" / "generated"
    output.mkdir(parents=True, exist_ok=True)
    write_source(output / "st_unicode_grapheme.c", grapheme, incb,
                 pictographic, sequences)


if __name__ == "__main__":
    main()
