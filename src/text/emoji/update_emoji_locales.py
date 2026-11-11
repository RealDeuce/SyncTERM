#!/usr/bin/env python3
"""Bundle pinned CLDR emoji names/keywords; normal builds are offline."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import struct
import urllib.request
import xml.etree.ElementTree as ET
import zipfile

import update_emoji_catalog as catalog

VERSION = "49"
URL = f"https://unicode.org/Public/cldr/{VERSION}/core.zip"
ARCHIVE_SHA256 = "78403d8d6fb1fed6e208bf9001bf5be00bb5abfd9f8110dae062bf49309d6a89"
LICENSE_SHA256 = "65eb33419e22a297550a811b5b535f5e532f9a7e0114b516c0ea18afd53c3804"
# Pin the uncompressed JSON so gzip implementations cannot affect the check.
SOURCE_SHA256 = "70122165a96b81b6d3bc8fc3d9f2208a2ff9a513f2c3bd881ca95a0ae6f21cb6"
SOURCE = f"src/text/emoji/data/cldr-{VERSION}-emoji.json.gz"
OUTPUT = "src/text/emoji/generated/st_emoji_locale_data.c"
LICENSE = "src/text/emoji/LICENSE.CLDR"


def key(text):
    return text.replace("_", "-").lower()


def sequence(text):
    return text.replace("\ufe0f", "").replace("\ufe0e", "")


def import_archive(data, root=catalog.ROOT):
    if hashlib.sha256(data).hexdigest() != ARCHIVE_SHA256:
        raise ValueError("CLDR archive does not match the upstream pin")
    archive = zipfile.ZipFile(io.BytesIO(data))
    eligible = {sequence(text) for text, _ in catalog.names(root)}
    locales = {}
    for directory in ("annotations", "annotationsDerived"):
        for path in sorted(archive.namelist()):
            if not path.startswith(f"common/{directory}/") or not path.endswith(".xml"):
                continue
            language = key(Path(path).stem)
            rows = locales.setdefault(language, {})
            tree = ET.fromstring(archive.read(path))
            if tree.findall(".//alias"):
                raise ValueError(f"Review CLDR annotation alias in {path}")
            for annotation in tree.findall("./annotations/annotation"):
                cp = sequence(annotation.get("cp", ""))
                if cp not in eligible:
                    continue
                value = annotation.text or ""
                if value == "↑↑↑":
                    continue
                if value == "∅∅∅":
                    raise ValueError(f"Review CLDR no-inheritance marker in {path}")
                row = rows.setdefault(cp, [None, None])
                field = 0 if annotation.get("type") == "tts" else 1
                row[field] = value if field == 0 else " | ".join(w.strip() for w in value.split("|"))
    supplemental = ET.fromstring(archive.read("common/supplemental/supplementalData.xml"))
    parents = {}
    for group in supplemental.findall("./parentLocales"):
        if group.get("component"):
            continue
        for parent in group.findall("parentLocale"):
            for locale in parent.get("locales").split():
                parents[key(locale)] = key(parent.get("parent"))
    metadata = ET.fromstring(archive.read("common/supplemental/supplementalMetadata.xml"))
    aliases = {key(a.get("type")): key(a.get("replacement").split()[0])
               for a in metadata.findall(".//languageAlias")}
    likely_tree = ET.fromstring(archive.read("common/supplemental/likelySubtags.xml"))
    likely = {key(a.get("from")): key(a.get("to")) for a in likely_tree.findall(".//likelySubtag")}
    return {"version": VERSION, "archive_sha256": ARCHIVE_SHA256,
            "locales": locales, "parents": parents, "aliases": aliases, "likely": likely}, archive.read("LICENSE")


def parent(locale, parents):
    return parents.get(locale, locale.rsplit("-", 1)[0] if "-" in locale else "root")


def resolve(tag, data):
    locales, aliases, likely, parents = (data[k] for k in ("locales", "aliases", "likely", "parents"))
    tag = aliases.get(tag, tag)
    parts = tag.split("-")
    replacement = aliases.get(parts[0], parts[0]).split("-")
    if replacement != parts[:1]:
        parts = replacement + parts[1:]
        tag = "-".join(parts)
    if tag in locales and tag != "root":
        return tag
    language = parts[0]
    script = next((p for p in parts[1:] if len(p) == 4 and p.isalpha()), None)
    region = next((p for p in parts[1:] if len(p) == 2 or (len(p) == 3 and p.isdigit())), None)
    # Regional parents are specified without the language's default script.
    default = likely.get(language, "").split("-")
    regional = "-".join(p for p in (language, region) if p)
    inherited = regional if script and len(default) > 1 and script == default[1] else tag
    if inherited in parents:
        while inherited in parents:
            inherited = parents[inherited]
            if inherited in locales and inherited != "root":
                return inherited
        if inherited == "root":
            return None
    guess = None
    for candidate in (tag, "-".join(p for p in (language, script) if p),
                      "-".join(p for p in (language, region) if p), language):
        if candidate in likely:
            guess = likely[candidate].split("-")
            break
    if guess is None:
        return None
    candidate = "-".join((language, script or guess[1], region or guess[2]))
    visited = set()
    while candidate != "root":
        if candidate in visited:
            raise ValueError("CLDR locale parent cycle")
        visited.add(candidate)
        if candidate in locales:
            return candidate
        if script and candidate == f"{language}-{script}" and len(default) > 1 and script != default[1]:
            return None
        candidate = parent(candidate, parents)
    return None


def lz4_block(data):
    """Deterministic greedy LZ4 block; final five bytes are always literals."""
    data = bytes(data)
    out = bytearray()
    positions = {}
    anchor = pos = 0
    last_match = len(data) - 12
    match_end = len(data) - 5

    def length(value):
        while value >= 255:
            out.append(255)
            value -= 255
        out.append(value)

    while pos <= last_match:
        word = data[pos:pos + 4]
        previous = positions.get(word, -65536)
        positions[word] = pos
        if pos - previous > 65535:
            pos += 1
            continue
        size = 4
        while pos + size < match_end and data[previous + size] == data[pos + size]:
            size += 1
        literals = pos - anchor
        out.append((min(literals, 15) << 4) | min(size - 4, 15))
        if literals >= 15:
            length(literals - 15)
        out.extend(data[anchor:pos])
        out.extend(struct.pack("<H", pos - previous))
        if size >= 19:
            length(size - 19)
        for i in range(pos + 1, pos + size):
            positions[data[i:i + 4]] = i
        pos += size
        anchor = pos
    literals = len(data) - anchor
    out.append(min(literals, 15) << 4)
    if literals >= 15:
        length(literals - 15)
    out.extend(data[anchor:])
    return bytes(out)


def source(root):
    raw = gzip.decompress((root / SOURCE).read_bytes())
    if hashlib.sha256(raw).hexdigest() != SOURCE_SHA256:
        raise ValueError("CLDR snapshot does not match the pinned input")
    if hashlib.sha256((root / LICENSE).read_bytes()).hexdigest() != LICENSE_SHA256:
        raise ValueError("CLDR license does not match the upstream notice")
    data = json.loads(raw)
    if data["version"] != VERSION or data["archive_sha256"] != ARCHIVE_SHA256:
        raise ValueError("CLDR snapshot provenance mismatch")
    return data


def generate(data, entries):
    languages = sorted(set(data["locales"]) - {"root"})
    ids = {sequence(text): i for i, (text, _) in enumerate(entries)}
    if len(ids) != len(entries) or len(entries) >= 65536:
        raise ValueError("Review the CLDR catalog's packed emoji indexes")
    out = [f"/* Generated by update_emoji_locales.py; CLDR {VERSION}, Unicode license. */",
           f"/* Catalog order SHA-256: {catalog_fingerprint(entries)} */",
           '#include "st_emoji_locale_internal.h"', ""]
    descriptors = []
    for language in languages:
        records = bytearray()
        for cp, fields in sorted(data["locales"][language].items(), key=lambda item: ids.get(item[0], -1)):
            if cp not in ids:
                continue
            records.extend(struct.pack("<H", ids[cp]))
            search_fields = list(fields)
            # Preserve the Unicode catalog's English labels while also making
            # CLDR's occasionally different English short names searchable.
            if language == "en" and fields[0] and fields[0] != entries[ids[cp]][1]:
                search_fields[1] = fields[0] + " | " + (fields[1] or "")
            for field in search_fields:
                records.extend((field or "").encode("utf-8") + b"\0")
        packed = lz4_block(records)
        symbol = "locale_" + language.replace("-", "_")
        out.append(f"static const unsigned char {symbol}[] = {{")
        for start in range(0, len(packed), 24):
            out.append("\t0x" + packed[start:start + 24].hex(",").replace(",", ",0x") + ",")
        out.append("};")
        ancestor = parent(language, data["parents"])
        while ancestor != "root" and ancestor not in languages:
            ancestor = parent(ancestor, data["parents"])
        parent_id = languages.index(ancestor) if ancestor in languages else -1
        descriptors.append(f'\t{{"{language}", {parent_id}, {symbol}, sizeof({symbol}), {len(records)}}},')
    out += ["const struct st_emoji_locale_data st_emoji_locales[] = {", *descriptors, "};",
            "const size_t st_emoji_locale_count = sizeof(st_emoji_locales) / sizeof(st_emoji_locales[0]);", "",
            "const struct st_emoji_locale_alias st_emoji_locale_aliases[] = {"]
    candidates = set(languages) | set(data["aliases"]) | set(data["likely"]) | set(data["parents"])
    candidates.update("-".join(v.split("-")[:2]) for v in data["likely"].values())
    for tag in list(candidates):
        parts = tag.split("-")
        default = data["likely"].get(parts[0], "").split("-")
        if len(parts) == 2 and len(parts[1]) in (2, 3) and len(default) > 1:
            candidates.add("-".join((parts[0], default[1], parts[1])))
    for candidate in sorted(candidates):
        resolved = resolve(candidate, data)
        if resolved is not None:
            out.append(f'\t{{"{candidate}", {languages.index(resolved)}}},')
    out += ["};", "const size_t st_emoji_locale_alias_count = sizeof(st_emoji_locale_aliases) / sizeof(st_emoji_locale_aliases[0]);", ""]
    return "\n".join(out)


def catalog_entries(root=catalog.ROOT):
    root = Path(root)
    coverage = json.loads((root / catalog.MANIFEST).read_text())
    supported = {"".join(chr(int(cp, 16)) for cp in s.split()) for s in coverage["sequences"]}
    return [(text, name) for text, name in catalog.names(root) if text in supported]


def catalog_fingerprint(entries):
    return hashlib.sha256("\n".join(text for text, _ in entries).encode()).hexdigest()


def validate_catalog(root=catalog.ROOT):
    with (Path(root) / OUTPUT).open(encoding="ascii") as stream:
        stream.readline()
        fingerprint = stream.readline().strip()
    expected = f"/* Catalog order SHA-256: {catalog_fingerprint(catalog_entries(root))} */"
    if fingerprint != expected:
        raise ValueError("Stale CLDR emoji indexes: run update_emoji_locales.py")


def generated(root=catalog.ROOT):
    return generate(source(root), catalog_entries(root))


def validate_repository(root=catalog.ROOT):
    if (Path(root) / OUTPUT).read_text(encoding="ascii") != generated(root):
        raise ValueError("Stale CLDR emoji catalog: run update_emoji_locales.py")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--fetch", action="store_true", help="refresh the pinned snapshot from upstream")
    parser.add_argument("--archive", type=Path, help="use a previously downloaded pinned core.zip")
    args = parser.parse_args()
    root = catalog.ROOT
    if args.fetch or args.archive:
        raw = args.archive.read_bytes() if args.archive else urllib.request.urlopen(URL).read()
        data, license_text = import_archive(raw, root)
        raw_json = json.dumps(data, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()
        digest = hashlib.sha256(raw_json).hexdigest()
        if digest != SOURCE_SHA256:
            raise ValueError("Filtered CLDR input pin changed; review the importer and upstream data")
        buffer = io.BytesIO()
        with gzip.GzipFile(filename="", mode="wb", fileobj=buffer, mtime=0) as stream:
            stream.write(raw_json)
        compressed = buffer.getvalue()
        (root / SOURCE).write_bytes(compressed)
        (root / LICENSE).write_bytes(license_text)
        print(f"CLDR snapshot SHA-256: {digest}")
    if args.check:
        validate_repository(root)
    else:
        (root / OUTPUT).write_text(generated(root), encoding="ascii")


if __name__ == "__main__":
    main()
