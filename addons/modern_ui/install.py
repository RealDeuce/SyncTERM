#!/usr/bin/env python3
"""Package/install the optional Modern UI without overwriting user scripts."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
import sys
import zipfile

ROOT = Path(__file__).resolve().parent


def files():
    return sorted((ROOT / "scripts").rglob("*.wren"))


def install(destination):
    copies = [(path, destination / path.relative_to(ROOT / "scripts")) for path in files()]
    # Check the complete install before changing any file, including entry points.
    for source, target in copies:
        if target.exists() and target.read_bytes() != source.read_bytes():
            raise ValueError(f"Existing script would be replaced: {target}")
    # Publish the override last: an interrupted install must not select a
    # frontend whose importable modules have not arrived yet.
    copies.sort(key=lambda pair: pair[0].name == "main_menu.wren")
    created = []
    try:
        for source, target in copies:
            if target.exists():
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            with target.open("xb") as stream:
                created.append(target)
                stream.write(source.read_bytes())
    except BaseException:
        for target in reversed(created):
            target.unlink()
        raise


def uninstall(destination):
    targets = [(path, destination / path.relative_to(ROOT / "scripts")) for path in files()]
    for source, target in targets:
        if target.exists() and target.read_bytes() != source.read_bytes():
            raise ValueError(f"Modified script will be preserved: {target}")
    for _, target in targets:
        if target.exists():
            target.unlink()


def package(output):
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in [ROOT / "README.md", ROOT / "LICENSE", ROOT / "install.py", *files()]:
            archive.write(path, "syncterm-modern-ui/" + path.relative_to(ROOT).as_posix())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for command in ("install", "uninstall"):
        commands.add_parser(command).add_argument("--scripts-dir", required=True, type=Path)
    commands.add_parser("package").add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        if args.command == "package":
            package(args.output)
        else:
            globals()[args.command](args.scripts_dir)
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
