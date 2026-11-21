#!/usr/bin/env python3
"""Select native RIP3 text at its actual pixel origin; restore both planes."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path

from st_migrated_control_test import connection
from st_rip3_connected_test import query
from st_rip3_extended_window_connected_test import window
from st_terminal_conformance_test import Terminal
from syncterm_fixture import receive


def run(args):
    with connection(args.executable, args.reports, encoding=args.encoding,
                    options="ScreenMode=Custom\nRIP=RIPv3\n",
                    settings="CustomColumns=80\nCustomRows=60\nCustomFontHeight=8\n"
                             "CustomAspectWidth=4\nCustomAspectHeight=3\n") as (client, root, peer, send):
        t = Terminal(client, root, peer, send)
        label = b"\x82|EST.rip" if args.encoding == "CP437" else "é|EST.rip".encode()
        # The showrips prologue must discard the entry grid for selection too.
        t.exchange(b"OLD-ENTRY-TEXT\x1b[m\r\n!|*\r\n" + label)
        for positioned in (False, True):
            t.case("rip3-selection-positioned" if positioned else "rip3-selection-after-reset")
            if positioned:
                t.exchange(query(b"$WORLD(CUR,640,480)$", at_cursor=True)
                           + window((19, 23, 99, 56), 10, 3, 0, 0) + label)
            # Position the cursor at home so one capture supplies geometry,
            # screen pixels and all state used below.
            before = t.exchange(b"\x1b[2 q\x1b[H")
            m = before.metadata
            ox, oy = m["pixel_cursor_x"], m["pixel_cursor_y"]
            cw = m["pixel_cursor_width"]
            if positioned:
                assert (ox, oy, cw) == (19, 23, 8)
            else:
                assert (ox, oy, cw) == (0, 0, 8)

            def point(x, y):
                return (m["destination_x"] + x*m["destination_width"]//m["plane_width"],
                        m["destination_y"] + y*m["destination_height"]//m["plane_height"])

            start, end = point(ox+1, oy+1), point(ox+4*cw+1, oy+1)
            for cancel in (False, True):
                client.clipboard("unchanged")
                client.mouse(*start, action=1, buttons=1)
                client.mouse(*end, buttons=1)
                selected = client.wait_for(lambda s: s.primary != before.primary)
                # Highlight must reach the actual current window, never the
                # hidden OLD-ENTRY-TEXT grid or a differently sized root cell.
                at = oy*m["plane_width"]+ox
                assert selected.primary[at] & 0xffffff == 0xa8
                if cancel:
                    client.key("escape")
                client.mouse(*end, action=1)
                if cancel:
                    after = client.wait_for(lambda s: s.primary == before.primary
                                            and s.alternate == before.alternate)
                    assert after.clipboard == "unchanged"
                    receive(peer, b"\x1b", root / "replies.bin")
                else:
                    after = client.wait_for(lambda s: s.clipboard == "é|EST")
                    assert after.primary == before.primary and after.alternate == before.alternate


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    parser.add_argument("encoding", choices=("CP437", "UTF-8"))
    parser.add_argument("--reports", type=Path, required=True)
    run(parser.parse_args())
