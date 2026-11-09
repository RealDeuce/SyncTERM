#!/usr/bin/env python3
"""Inspect reference fonts; optional amitools dependency, never part of the build."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
import hashlib
import html
import importlib.util
import json
from pathlib import Path
import re
import struct
import subprocess


ROOT = Path(__file__).resolve().parent


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def native(path):
    from amitools.binfmt.hunk.BinFmtHunk import BinFmtHunk
    from amitools.binfmt.Relocate import Relocate

    image = BinFmtHunk().load_image(str(path))
    if len(image.segments) != 1:
        raise ValueError(f"{path}: expected one disk-font hunk")
    base = 0x200000
    data = bytes(Relocate(image).relocate([base])[0])
    if data[18:20] != b"\x0f\x80":
        raise ValueError(f"{path}: unexpected disk-font ID")
    tf = 58
    word = lambda offset: struct.unpack_from(">H", data, tf + offset)[0]
    ptr = lambda offset: struct.unpack_from(">I", data, tf + offset)[0] - base
    height, width, baseline = word(20), word(24), word(26)
    flags = data[tf + 23]
    lo, hi = data[tf + 32:tf + 34]
    bits, loc = ptr(34), ptr(40)
    space, kern = ptr(44), ptr(48)
    glyphs = []
    for i in range(hi - lo + 2):
        offset, raster_width = struct.unpack_from(">HH", data, loc + i * 4)
        k = struct.unpack_from(">h", data, kern + i * 2)[0] if kern >= 0 else 0
        s = struct.unpack_from(">h", data, space + i * 2)[0] if space >= 0 else width
        bearing = k if flags & 32 else (width - raster_width) // 2
        advance = k + s if flags & 32 else width
        rows = []
        for y in range(height):
            row = 0
            for x in range(raster_width):
                bit = offset + x
                row = (row << 1) | bool(
                    data[bits + y * word(38) + bit // 8] & (128 >> (bit % 8)))
            rows.append(row)
        points = {(bearing + x, y - baseline)
                  for y, row in enumerate(rows) for x in range(raster_width)
                  if row & (1 << (raster_width - x - 1))}
        glyphs.append({"advance": advance, "points": points,
                       "raw_raster": [raster_width, rows], "raw_spacing": [k, s]})
    return {"height": height, "width": width, "baseline": baseline,
            "flags": flags, "lo": lo, "hi": hi, "default": glyphs[-1],
            "glyphs": {c: glyphs[c - lo] if lo <= c <= hi else glyphs[-1]
                       for c in range(256)},
            "versions": [v.decode("latin1") for v in
                         re.findall(rb"\$VER:[^\x00\r\n]+", path.read_bytes())]}


def builtins():
    spec = importlib.util.spec_from_file_location("yaff_reader", ROOT.parent / "generate_fonts.py")
    reader = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(reader)
    manifest = json.loads((ROOT.parent / "data/manifest.json").read_text())
    fonts = {}
    for entry in manifest["fonts"]:
        props, raw = reader.read_font(ROOT.parent / "data" / entry["file"])
        width = int(props.get("cell-size", props.get("bounding-box", "8x8")).split("x")[0])
        height = entry["height"]
        old_baseline = height + int(props.get("shift-up", "0")) - 1
        baseline = entry.get("baseline", old_baseline)
        glyphs = []
        for labels, rows, gp in raw:
            raster_width = max(map(len, rows), default=0)
            left = int(gp.get("left-bearing", props.get("left-bearing", "0")))
            right = int(gp.get("right-bearing", props.get("right-bearing", "0")))
            advance = left + raster_width + right if props.get("spacing") == "proportional" else width
            top = -int(gp.get("shift-up", props.get("shift-up", "0"))) + old_baseline - baseline - height + 1
            points = {(left + x, top + y) for y, row in enumerate(rows)
                      for x, value in enumerate(row) if value == "@"}
            glyphs.append((labels, {"advance": advance, "points": points}))
        default = next((g for labels, g in glyphs if "default" in labels),
                       {"advance": width, "points": set()})
        mapping = dict.fromkeys(range(256), default)
        for prefix in ("u+", "0x"):
            for labels, g in glyphs:
                for label in labels:
                    if label.startswith(prefix):
                        try:
                            code = int(label[len(prefix):], 16)
                        except ValueError:
                            continue
                        if code < 256:
                            mapping[code] = g
        key = entry["name"].lower().removesuffix(".font") + "/" + str(height)
        fonts[key] = {"height": height, "width": entry.get("cell_width", width),
                      "baseline": max(0, baseline), "glyphs": mapping,
                      "default": default, "source": entry["file"]}
    return fonts


def spf(path):
    data = path.read_bytes()
    if data[:4] != b"SPF2":
        raise ValueError(f"{path}: expected SPF2")
    height, lo, baseline = data[4:7]
    count = int.from_bytes(data[7:9], "little")
    offset = 9
    glyphs = {}
    for c in range(lo, lo + count):
        advance, width = data[offset:offset + 2]
        offset += 2
        stride = (width + 7) // 8
        points = {(x, y - baseline) for y in range(height) for x in range(width)
                  if data[offset + y * stride + x // 8] & (128 >> (x % 8))}
        offset += stride * height
        glyphs[c] = {"advance": advance, "points": points}
    if offset != len(data):
        raise ValueError(f"{path}: unexpected trailing/truncated SPF data")
    return {"height": height, "width": None, "baseline": baseline, "glyphs": glyphs}


def canonical(font):
    digest = 0xcbf29ce484222325

    def word(value):
        nonlocal digest
        for byte in ((value >> 8) & 255, value & 255):
            digest = ((digest ^ byte) * 0x100000001b3) & 0xffffffffffffffff

    for value in (font["height"], font["baseline"], font["width"]):
        word(value)
    for c in range(256):
        glyph = font["glyphs"][c]
        word(glyph["advance"])
        word(len(glyph["points"]))
        for x, y in sorted(glyph["points"], key=lambda p: (p[1], p[0])):
            word(x)
            word(y + font["height"] - 1)
    return f"{digest:016x}"


def shape(points):
    if not points:
        return set()
    x0 = min(x for x, y in points)
    y0 = min(y for x, y in points)
    return {(x - x0, y - y0) for x, y in points}


def compare(a, b):
    chars = sorted(a["glyphs"].keys() & b["glyphs"].keys())
    result = {"byte_count": len(chars), "first_byte": min(chars), "last_byte": max(chars),
              "metrics": {k: [a[k], b[k]] for k in ("height", "width", "baseline")
                          if a.get(k) != b.get(k)}}
    for name, predicate in (
        ("shape", lambda x, y: shape(x["points"]) != shape(y["points"])),
        ("advance", lambda x, y: x["advance"] != y["advance"]),
        ("placed_ink", lambda x, y: x["points"] != y["points"]),
    ):
        changed = [c for c in chars if predicate(a["glyphs"][c], b["glyphs"][c])]
        result[name] = changed
        result["ascii_" + name] = [c for c in changed if 32 <= c <= 126]
        result["latin1_" + name] = [c for c in changed if 160 <= c <= 255]
        if "default" in a and "default" in b:
            result["default_" + name] = predicate(a["default"], b["default"])
    return result


def metadata(font):
    return {k: v for k, v in font.items() if k not in ("glyphs", "default")}


def specimens(refs, catalog, supplied):
    width, height = 1500, 1050
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}">',
             '<rect width="100%" height="100%" fill="#f7f6f1"/>']

    def label(x, y, text, size=16):
        parts.append(f'<text x="{x}" y="{y + size}" font-family="sans-serif" '
                     f'font-size="{size}" fill="#283238">{html.escape(text)}</text>')

    label(25, 18, "Amiga disk references / compiled SyncTERM / supplied native fonts")
    label(25, 48, "Literal unstyled glyphs at 2x scale. Blue line = common pen baseline; widths and advances remain native.", 13)
    columns = [("Workbench 1.3", 145), ("Workbench 2.x / 3.1", 480),
               ("Compiled SyncTERM", 815), ("syncterm-skypix native", 1150)]
    for title, x in columns:
        label(x, 83, title)
    keys = ["courier/11", "diamond/12", "ruby/8", "emerald/17", "garnet/9",
            "helvetica/11", "opal/9", "sapphire/14", "times/11", "topaz/11"]
    for i, key in enumerate(keys):
        y = 120 + i * 86
        label(25, y + 15, key)
        fonts = [refs["workbench-1.3"][key], refs["workbench-2.1-3.1"][key],
                 catalog[key], supplied[key]]
        for (_, x), font in zip(columns, fonts):
            baseline = y + 48
            parts.append(f'<path d="M{x} {baseline + 2}h300" stroke="#9fbfc9"/>')
            pen = x
            pixels = []
            for c in b"Amiga 01!?":
                glyph = font["glyphs"][c]
                for gx, gy in sorted(glyph["points"]):
                    pixels.append(f'M{pen + gx * 2} {baseline + gy * 2}h2v2h-2z')
                pen += glyph["advance"] * 2
            parts.append('<path d="' + ''.join(pixels) + '" fill="#19292e"/>')
            label(x, y + 59, f"nominal {font['width']}px; baseline {font['baseline']}; advance {(pen - x) // 2}px", 13)
    label(25, height - 46, "Reference files are not built or installed. Disk and supplied-native columns agree for all 30 stock strikes.", 13)
    return '\n'.join(parts + ['</svg>']) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--builtins", required=True, type=Path,
                        help="CMake-built st_amiga_font_test executable")
    parser.add_argument("--skypix", required=True, type=Path,
                        help="local syncterm-skypix checkout (read only)")
    parser.add_argument("--check", action="store_true", help="verify saved reports without writing")
    args = parser.parse_args()
    manifest = json.loads((ROOT / "manifest.json").read_text())
    refs = {"workbench-1.3": {}, "workbench-2.1-3.1": {}}
    for entry in manifest["files"]:
        path = ROOT / entry["path"]
        if sha256(path) != entry["sha256"] or path.stat().st_size != entry["size"]:
            raise ValueError(f"reference identity mismatch: {path}")
        if path.name.isdecimal():
            key = path.parent.name.lower() + "/" + path.name
            refs[Path(entry["path"]).parts[0]][key] = native(path)
    catalog = builtins()
    lines = subprocess.check_output([str(args.builtins.resolve()), "--builtins"], text=True)
    verified = set()
    for line in lines.splitlines():
        name, height, width, baseline, digest = line.split("\t")
        key = name.lower().removesuffix(".font") + "/" + height
        font = catalog[key]
        if (font["width"], font["baseline"], canonical(font)) != (int(width), int(baseline), digest):
            raise ValueError(f"compiled catalog does not match YAFF reconstruction: {key}")
        verified.add(key)
    if verified != catalog.keys():
        raise ValueError("incomplete compiled catalog verification")
    report = {"method": "All 256 native byte mappings, including default substitutions, are compared. Shape removes translation; placed ink uses native bearing and y=row-baseline; advance uses kern+space for proportional fonts and nominal width for monospaced fonts. YAFF reconstruction is verified against actual compiled FNV-1a-64 hashes for all 39 built-ins. SPF coverage is ASCII 32..126 only; SPF placed ink assumes left alignment because SPF2 carries no bearing field, and is not a claim about RGTerm rendering.",
              "compiled_catalog_entries_verified": len(verified),
              "reference_sources": [
                  {"id": source["id"], "version": source["version"],
                   "retained_files": sum(entry["source"] == source["id"] or
                                         source["id"] in entry.get("verified_identical_in", [])
                                         for entry in manifest["files"]),
                   "bitmap_strikes": sum(Path(entry["path"]).name.isdecimal() and
                                         (entry["source"] == source["id"] or
                                          source["id"] in entry.get("verified_identical_in", []))
                                         for entry in manifest["files"])}
                  for source in manifest["sources"]],
              "references": {v: {k: metadata(f) for k, f in fs.items()} for v, fs in refs.items()},
              "builtins": {k: metadata(f) for k, f in catalog.items()},
              "workbench_changes": {k: compare(refs["workbench-1.3"][k], refs["workbench-2.1-3.1"][k]) for k in sorted(refs["workbench-1.3"])},
              "reference_to_builtin": {v: {k: compare(f, catalog[k]) for k, f in sorted(fs.items())} for v, fs in refs.items()}}
    commit = subprocess.check_output(["git", "-C", str(args.skypix), "rev-parse", "HEAD"], text=True).strip()
    supplied = {"repository": "https://github.com/SpitfireTN/syncterm-skypix",
                "commit": commit, "files": []}
    natives = {}
    paths = sorted((args.skypix / "fonts/amiga-font").glob("*/*"))
    paths += sorted((args.skypix / "fonts/rgterm-spf").glob("*.SPF"))
    for path in paths:
        if not path.is_file() or not (path.name.isdecimal() or path.suffix == ".SPF"):
            continue
        is_native = path.name.isdecimal()
        if is_native:
            key = path.parent.name.lower() + "/" + path.name
            font = native(path)
            natives[key] = font
        else:
            name, height = path.stem.rsplit("_", 1)
            key = name.lower() + "/" + str(int(height))
            font = spf(path)
        matches = []
        for entry in manifest["files"]:
            if is_native and entry["sha256"] == sha256(path):
                matches.append(entry["path"])
        row = {"path": path.relative_to(args.skypix).as_posix(), "strike": key,
               "format": "native" if is_native else "SPF2", "sha256": sha256(path),
               "metadata": metadata(font), "exact_reference_files": matches,
               "reference_comparisons": {v: compare(fs[key], font) for v, fs in refs.items() if key in fs},
               "builtin_comparison": compare(catalog[key], font)}
        if not is_native and key in natives:
            row["supplied_native_comparison"] = compare(natives[key], font)
        supplied["files"].append(row)
    for filename, content in (("comparison.json", report), ("skypix-comparison.json", supplied)):
        rendered = json.dumps(content, indent=2) + "\n"
        rendered = re.sub(r"\[\s*(-?\d+(?:,\s*-?\d+)*)\s*\]",
                          lambda m: "[" + ", ".join(re.findall(r"-?\d+", m[1])) + "]", rendered)
        path = ROOT / filename
        if args.check:
            if path.read_text() != rendered:
                raise ValueError(f"stale report: {path}")
        else:
            path.write_text(rendered)
    preview = ROOT / "specimens.svg"
    rendered = specimens(refs, catalog, natives)
    if args.check:
        if preview.read_text() != rendered:
            raise ValueError(f"stale preview: {preview}")
    else:
        preview.write_text(rendered)
    print(f"Verified {len(manifest['files'])} reference files, {len(verified)} compiled fonts, "
          f"and {len(supplied['files'])} supplied native/SPF files at {commit}.")


if __name__ == "__main__":
    main()
