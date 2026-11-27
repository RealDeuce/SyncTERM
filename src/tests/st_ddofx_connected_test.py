"""DDO F/X over a live connection with ordinary ANSI before and after commands."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path

from st_migrated_control_test import connection
from st_terminal_conformance_test import Terminal


def run(args):
    ident = b"\x1d{dLITERM}"
    reply = b"LITERM Ver. 2.00\xff"
    with connection(args.executable, args.reports, encoding=args.encoding,
                    options="Graphics=DDO F/X 2.0\n", settings="AudioModes=0\n") as fixture:
        t = Terminal(*fixture)
        for split in range(len(ident) + 1):
            t.send(ident[:split])
            t.exchange(ident[split:], reply)
        t.exchange(b"\x1d{dliterm}\x1d{DLITERM}", reply)
        t.exchange(b"\x1d{dMUSICON}\x1d{mMISSING.MOD}\x1d{dGETMUSIC}", b"NO\xffNONE\xff")
        t.exchange(b"\x1d{MGVOL}\x1d{MSVOL,20}\x1d{MMVOL,-5}\x1d{MGVOL}", b"64\xff15\xff")
        glyph = "é".encode("utf-8" if args.encoding == "UTF-8" else "cp437")
        t.check("framing amid ANSI", b"\x1b[2;" + ident + b"3H" + glyph + ident + ident + b"B",
                (5, 2), [(3, 2, "éB")], expected=reply * 3)
        t.check("ignore and hear", b"A\x1d{~IGNORCMD}" + ident + b"B\x1d{~HEARCMD}" + ident,
                (3, 1), [(1, 1, "AB")], expected=reply)
        t.check("recover and continue", b"A\x1d{" + b"x" * 251 + b"}B\x1dX" + ident,
                (4, 1), [(1, 1, "ABX")], expected=reply)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("encoding", choices=("CP437", "UTF-8"))
    parser.add_argument("--reports", type=Path, required=True)
    run(parser.parse_args())
