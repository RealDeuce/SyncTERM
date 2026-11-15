#!/usr/bin/env python3
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
"""Exercise compiled name recognition against the pinned source inventory."""
import argparse
import json
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    ledger = json.loads((root / "src/tests/rip3/requirements.json").read_text(encoding="utf-8"))
    prefix = "version/3.0/ripscrip/"
    source = "".join(row["text"] for row in ledger["requirements"]
                     if row["source"] == prefix + "9.1-text-variable-reference.md")
    names = set()
    for line in source.splitlines():
        if line.startswith("| `$"):
            first_cell = line.split("|")[1]
            names.update(re.findall(r"\$([A-Z][A-Z0-9_]*)(?=[($])", first_cell))
    if not {"IFS", "NULL", "T", "BACKSTAT", "WORLD"}.issubset(names):
        raise ValueError("missing pinned reference inventory")
    for family, notation in (("APP", "$APP0$`-`$APP9$"),
                             ("SAVE", "$SAVE0-9$"),
                             ("RESTORE", "$RESTORE0-9$")):
        if notation not in source:
            raise ValueError("review changed fixed-slot family: " + family)
        names.update(family + str(index) for index in range(10))
    for document, additions in (
            ("5.1-text-variables.md", ("SHIFT", "CTRL", "HOSTDIR", "XFER", "P")),
            ("7.1-data-tables.md", ("PROTECT",))):
        text = "".join(row["text"] for row in ledger["requirements"]
                       if row["source"] == prefix + document)
        for name in additions:
            if not re.search(r"(?<![A-Z])" + name + r"(?![A-Z])", text):
                raise ValueError("missing attested extra processor: " + name)
            names.add(name)
    subprocess.run([str(args.executable.resolve()), *sorted(names)], check=True)
    print(f"Compiled RIP3 recognizer accepts {len(names)} pinned built-in names")


if __name__ == "__main__":
    main()
