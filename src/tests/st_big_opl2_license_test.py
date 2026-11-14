#!/usr/bin/env python3
"""Full Opal MIT notice reachable through the normal offline license UI."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "testing"))
from syncterm_fixture import fixture, click_text
from syncterm_control import SHIFT


def body(frame):
    return " ".join(line.split("│")[1].strip() for line in frame.lines if "│" in line).split()


def run(executable, reports):
    expected = (Path(__file__).resolve().parents[1] / "media" / "opl2" / "LICENSE.OPAL").read_text().split()
    with fixture(executable, reports=reports) as (client, root):
        client.wait_text("Directory: Name")
        client.key("tab", SHIFT)
        menu = client.wait_text("Licenses")
        click_text(client, menu, "Licenses")
        listing = client.wait_text("Opal YM3812 adaptation (MIT)")
        click_text(client, listing, "Opal YM3812 adaptation (MIT)")
        first = body(client.wait_text("Copyright (c) 2026 Bitdancer"))
        client.key("end")
        last = body(client.wait_text("SOFTWARE."))
        overlap = next(n for n in range(min(len(first), len(last)), 0, -1) if first[-n:] == last[:n])
        assert first + last[overlap:] == expected, (first, last, expected)
        client.key("escape")
        client.wait_text("Opal YM3812 adaptation (MIT)")
        client.key("escape")
        client.wait_text("SyncTERM Settings")
        assert b"[wren" not in client.stderr, bytes(client.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("executable")
    parser.add_argument("--reports", default="reports/test-control")
    args = parser.parse_args()
    run(args.executable, args.reports)
