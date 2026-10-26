#!/usr/bin/env python3
"""Extract the exact legacy RIP bitmap and CHR font arrays, without ciolib."""

# Copyright (c) 2026 Stephen Hurd
# SPDX-License-Identifier: BSD-2-Clause

import argparse
import hashlib
from pathlib import Path

NAMES = (
    "bgifnt8x8", "Triplex", "Small", "Sans", "Gothic", "Script",
    "Simplex", "TriplexScript", "Complex", "European", "Bold",
)
TEXT_NAMES = ("ripfnt7x8", "ripfnt7x14", "ripfnt16x14")


def extract_array(source: str, name: str) -> str:
    marker = source.index(f"{name}[")
    start = source.rfind("\n", 0, marker) + 1
    brace = source.index("{", marker)
    depth = 1
    end = brace + 1
    while depth:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
        end += 1
    if source[end] != ";":
        raise ValueError(f"missing terminator for {name}")
    declaration = source[start:end + 1]
    return declaration


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("legacy_ripper", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    source = args.legacy_ripper.read_text(encoding="utf-8")
    begin = source.index("static unsigned char       bgifnt8x8[2048]")
    end = source.index("static const int           stroke_mults", begin)
    arrays = source[begin:end]
    for name in NAMES:
        if f"{name}[" not in arrays:
            raise ValueError(f"missing RIP font array {name}")
    arrays = arrays.replace("static unsigned char       bgifnt8x8",
                            "static const unsigned char bgifnt8x8", 1)
    # The last two terminal fonts sit amid the BGI arrays in legacy source.
    # Emit them once, separately, so their storage is const and private.
    for name in TEXT_NAMES[1:]:
        arrays = arrays.replace(extract_array(source, name), "", 1)
    text_arrays = [extract_array(source, name).replace(
        "unsigned char", "const uint8_t", 1) for name in TEXT_NAMES]
    text_arrays = [item if item.lstrip().startswith("static ")
                   else "static " + item for item in text_arrays]
    low_char = extract_array(source, "low_char")
    metrics_start = source.index("static const struct                metric font_metrics")
    metrics_else = source.index("#else /* if 0 */", metrics_start)
    metrics_end = source.index("#endif /* if 0 */", metrics_else)
    metrics = (
        "static const struct cterm_rip_font_metric button_metrics[11][10] = {\n"
        + source[metrics_else + len("#else /* if 0 */"):metrics_end].strip()
        + "\n};\n"
    )
    digest = hashlib.sha256(source[begin:end].encode("utf-8")).hexdigest()
    text_digest = hashlib.sha256("\n".join(text_arrays).encode("utf-8"))\
        .hexdigest()
    header = (
        "/* Exact RIPterm bitmap and CHR font arrays, mechanically extracted.\n"
        f" * Source-array SHA-256: {digest}\n"
        f" * Text-font SHA-256: {text_digest}\n"
        " * C code copyright (c) 2026 Stephen Hurd.\n"
        " * Embedded font data copyright (c) 1987,1988 Borland International.\n"
        " * SPDX-License-Identifier: BSD-2-Clause\n"
        " */\n\n"
        '#include "cterm_rip_fonts.h"\n\n'
    )
    table = [
        "\nstruct rip_font_array {",
        "\tconst uint8_t *data;",
        "\tsize_t size;",
        "};",
        "",
        "static const struct rip_font_array rip_font_arrays[] = {",
    ]
    table.extend(f"\t{{{name}, sizeof({name})}}," for name in NAMES)
    table.extend((
        "};", "", "const uint8_t *",
        "cterm_rip_font_data(int number, size_t *size)",
        "{",
        "\tif (size == NULL || number < 0 || number >= 11)",
        "\t\treturn NULL;",
        "\t*size = rip_font_arrays[number].size;",
        "\treturn rip_font_arrays[number].data;",
        "}", "",
        "const uint8_t *",
        "cterm_rip_text_font_data(int style, size_t *size)",
        "{",
        "\tif (size == NULL)",
        "\t\treturn NULL;",
        "\tswitch (style) {",
        "\tcase 1: *size = sizeof(ripfnt7x8); return ripfnt7x8;",
        "\tcase 3: *size = sizeof(ripfnt7x14); return ripfnt7x14;",
        "\tcase 4: *size = sizeof(ripfnt16x14); return ripfnt16x14;",
        "\tdefault: return NULL;",
        "\t}",
        "}", "",
        "const struct cterm_rip_font_metric *",
        "cterm_rip_font_button_metric(int number, int size)",
        "{",
        "\tif (number < 0 || number >= 11 || size < 1 || size > 10)",
        "\t\treturn NULL;",
        "\treturn &button_metrics[number][size - 1];",
        "}", "",
        "bool",
        "cterm_rip_button_char_descends(uint8_t ch)",
        "{",
        "\treturn low_char[ch] != 0;",
        "}", "",
    ))
    args.output.write_text(header + "\n\n".join(text_arrays)
                           + "\n\n" + arrays + "\n\n" + low_char
                           + "\n\n" + metrics + "\n".join(table),
                           encoding="utf-8")


if __name__ == "__main__":
    main()
