#!/usr/bin/env python3
"""Drive USB HID descriptors and reports through a real session and mapper."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import socket
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "testing"))
from syncterm_fixture import application, click_text, fixture, listener

# Report 1 contains seven anonymous buttons, X, an eight-way hat with null
# position 8, and an explicitly identified D-pad up button.
DESCRIPTOR = bytes.fromhex("""
    05 01 09 05 a1 01 85 01 05 09 19 01 29 07 15 00 25 01
    75 01 95 07 81 02 75 01 95 01 81 03
    05 01 09 30 26 ff 00 75 08 81 02
    09 39 25 07 75 04 81 42 81 03
    09 90 25 01 75 01 81 02 75 07 81 03 c0
""")
HOOK = '''
import "syncterm" for Hook, Conn
Hook.onController {|e|
  if (e["type"] == 3 && e["code"] == 0 && e["value"] == -32768) {
    Conn.sendBytes("left")
    return true
  }
  return false
}
'''


def exact(peer, expected):
    data = bytearray()
    while len(data) < len(expected):
        block = peer.recv(1)  # Parse across arbitrary transport boundaries.
        assert block, (data, expected)
        data.extend(block)
    assert data == expected, (bytes(data), expected)


def records(*rows):
    return b"".join(b"\x1b[=" + b";".join(str(x).encode() for x in row) + b"J" for row in rows)


def device(n, sample=None, *, mapped=False):
    caps = [(3, 0, 65535, 65534), (3, 16, 1, 2), (3, 17, 1, 2), (1, 544, 0, 1)]
    if mapped:
        caps.insert(0, (1, 304, 0, 1))
    rows = [(1, n, 2, 1), *((2, n, *cap) for cap in caps)]
    if sample is not None:
        rows.extend((3, n, t, code, value) for (t, code, _, _), value in zip(caps, sample))
    return records(*rows, (4, n))


def report(client, slot, button=0, axis=128, hat=8, up=0):
    client.request(7, bytes([2, slot, 1, button, axis, hat, up]))


def connected(executable, encoding, reports):
    with listener() as server:
        entries = (f"[Controllers]\nConnectionType=Raw\nAddress=127.0.0.1\nPort={server.getsockname()[1]}\n"
                   f"Encoding={encoding}\nScreenMode=80x25\nGraphics=None\n")
        with fixture(executable, entries=entries, script=HOOK, reports=reports) as (client, _):
            client.wait_text("Controllers")
            client.key("enter")
            with server.accept()[0] as peer:
                peer.settimeout(5)
                client.wait_for(lambda s: s.application.get("connected") == 1)
                peer.sendall(b"\x1b[<0c\x1b[=6$p\x1b[=6h\x1b[=6h\x1b[=6$p")
                exact(peer, b"\x1b[<0;1;2;3;4;5;6;7;8;9c\x1b[=6;2$y\x1b[=6J\x1b[=6;1$y")
                client.request(7, b"\x01\x00" + DESCRIPTOR)
                exact(peer, device(1))
                report(client, 0, button=64)
                exact(peer, records((3,1,3,0,0), (3,1,3,16,0), (3,1,3,17,0), (3,1,1,544,0), (4,1)))
                client.request(7, b"\x01\x01" + DESCRIPTOR)
                exact(peer, device(2))
                report(client, 0, axis=0, hat=1, up=1)
                exact(peer, b"left" + records((3,1,3,16,2), (3,1,3,17,1), (3,1,1,544,1), (4,1)))
                peer.sendall(b"\x1b[=1h\x1b[=1$p")
                exact(peer, b"\x1b[=1;1$y")
                client.physical_key(30, True)
                exact(peer, b"\x1b[=30K")
                report(client, 0, axis=255, hat=1, up=1)
                exact(peer, records((3,1,3,0,65534), (4,1)))
                client.physical_key(30, False)
                exact(peer, b"\x1b[=30k")
                peer.sendall(b"\x1b[=6l\x1b[=6h")
                exact(peer, device(3, (65534,2,1,1)) + device(4) + records((6,)))
                client.request(7, bytes([4, 0, 6, 1, 0x30, 1]))  # Button 7 -> BTN_SOUTH (304).
                # Both matching devices acquire new wire IDs. The held D-pad and
                # known current axis survive remapping; device two is unsampled.
                exact(peer, records((5,3)) + device(5, (0,65534,2,1,1), mapped=True)
                      + records((5,4)) + device(6, mapped=True))
                report(client, 0, button=64, axis=255, hat=1, up=1)
                exact(peer, records((3,5,1,304,1), (4,5)))
                client.event(6, 0)
                exact(peer, records((5,5), (5,6)))
                report(client, 0, axis=128)
                client.event(6, 1)
                exact(peer, device(7, (0,0,0,0,0), mapped=True) + device(8, mapped=True))
                report(client, 0, button=64)
                exact(peer, records((3,7,1,304,1), (4,7)))
                client.request(7, b"\x03\x00")
                exact(peer, records((5,7)))

                client.request(7, bytes([5, 0]))
                caps = [(3, code, 65535, 65534) for code in (0, 1, 3, 4)]
                caps.extend((3, code, 0, 65534) for code in (2, 5))
                caps.extend((1, code, 0, 1) for code in
                            (544, 545, 546, 547, 315, 314, 317, 318, 310, 311, 304, 305, 308, 307, 316))
                exact(peer, records((1,9,2,3), *((2,9,*cap) for cap in caps), (4,9)))
                neutral = bytes([2,0,0,20]) + bytes(18)
                client.request(7, neutral)
                exact(peer, records(*((3,9,t,code,0) for t,code,_,_ in caps), (4,9)))
                client.request(7, bytes([2,0,0,20,1,16,128,255]) + bytes(14))
                exact(peer, records((3,9,3,2,32896), (3,9,3,5,65534), (3,9,1,544,1), (3,9,1,304,1), (4,9)))
                report(client, 1, button=64)
                exact(peer, records((3,8,1,304,1), (3,8,3,0,0), (3,8,3,16,0), (3,8,3,17,0), (3,8,1,544,0), (4,8)))
                client.request(7, neutral)
                exact(peer, records((3,9,3,2,0), (3,9,3,5,0), (3,9,1,544,0), (3,9,1,304,0), (4,9)))
                client.request(7, bytes([3, 0]))
                exact(peer, records((5,9)))
                peer.sendall(b"\x1bc\x1b[=6$p")
                exact(peer, b"\x1b[=6;2$y")
                peer.shutdown(socket.SHUT_RDWR)


def choose(client, text):
    shot = client.wait_text(text)
    click_text(client, shot, text)


def scroll_to(client, label):
    client.key("home")
    for _ in range(48):
        shot = client.capture()
        if label in shot.text:
            click_text(client, shot, label)
            return
        client.key("down")
    raise AssertionError(f"Missing {label!r} in controller inputs")


def mapper(executable, reports):
    with fixture(executable, reports=reports) as (client, root):
        def enter(c):
            choose(c, "Program Settings")
            choose(c, "Program Settings")
            c.wait_text("Main Interface")
            # This row is below the fold on short screens.
            for _ in range(19):
                shot = c.capture()
                if "Controller Mapping" in shot.text:
                    choose(c, "Controller Mapping")
                    break
                c.key("down")
            c.wait_text("Press a control to assign")

        client.request(7, b"\x01\x00" + DESCRIPTOR)
        report(client, 0)
        enter(client)
        def check_help(text):
            client.key("f1")
            client.wait_text(text)
            client.key("escape")

        check_help("Choose what a BBS receives")
        choose(client, "Action pad south")
        client.wait_text("Choose control for Action pad south")
        check_help("Release held buttons")
        client.wait_text("Press a button or push one direction")
        report(client, 0, axis=255)
        report(client, 0, button=64)
        client.wait_for(lambda s: any("Button 7" in row and "Action pad south" in row for row in s.text.splitlines()))

        choose(client, "Press a control to assign")
        client.wait_text("Esc cancels")
        report(client, 0, button=65)
        client.wait_text("Send Button 1 to the BBS as")
        check_help("Choose the BBS input")
        choose(client, "Action pad east")
        client.wait_for(lambda s: any("Button 1" in row and "Action pad east" in row for row in s.text.splitlines()))

        choose(client, "Action pad east")
        choose(client, "Preview current control")
        client.wait_text("Pressed")
        check_help("Move this control")
        report(client, 0, button=64)
        client.wait_text("Released")
        client.key("escape")
        for axis, hat, description in [(128, 1, "Up-right"), (255, 1, "32767")]:
            choose(client, "Press a control to assign")
            client.wait_text("Esc cancels")
            report(client, 0, button=64, axis=axis, hat=hat)
            client.wait_text(description)
            client.key("escape")
            client.wait_text("Browse physical controls")

        choose(client, "Action pad east")
        choose(client, "Clear assignment")
        client.wait_for(lambda s: any("Unassigned" in row and "Action pad east" in row for row in s.text.splitlines()))
        choose(client, "Browse physical controls")
        choose(client, "Button 1")
        choose(client, "Action pad south")
        client.wait_text("Move Action pad south")
        client.key("escape")
        client.wait_for(lambda s: any("Button 7" in row and "Action pad south" in row for row in s.text.splitlines()))
        report(client, 0, axis=0)
        scroll_to(client, "Left trigger button")
        client.wait_text("Choose control for Left trigger button")
        report(client, 0, button=2, axis=128)
        client.wait_for(lambda s: any("Button 2" in row and "Left trigger button" in row for row in s.lines))
        report(client, 0, axis=0)
        scroll_to(client, "Left trigger axis")
        client.wait_text("Choose control for Left trigger axis")
        # Button 2 precedes the axis in the very same report.
        report(client, 0, button=2, axis=128)
        client.wait_for(lambda s: any("X axis" in row and "Left trigger axis" in row for row in s.lines))
        scroll_to(client, "D-pad down")
        client.wait_text("Choose control for D-pad down")
        report(client, 0, hat=3)  # Wait for a single direction before assigning.
        report(client, 0, hat=4)
        client.wait_for(lambda s: any("D-pad down" in row and "Hat 1 down" in row for row in s.lines))
        report(client, 0)
        scroll_to(client, "Press a control to assign")
        client.wait_text("Esc cancels")
        report(client, 0, hat=2)
        client.wait_text("Send Hat 1 (Right) to the BBS as")
        scroll_to(client, "D-pad right")
        client.wait_for(lambda s: any("D-pad right" in row and "Hat 1 right" in row for row in s.lines))
        client.close()

        # Use the saved UI assignments in a fresh connected session.
        with listener() as server:
            (root / "syncterm.lst").write_text(
                f"[Gamepad]\nConnectionType=Raw\nAddress=127.0.0.1\nPort={server.getsockname()[1]}\n")
            with application(executable, root, reports=reports) as session:
                session.wait_text("Gamepad")
                session.key("enter")
                with server.accept()[0] as peer:
                    peer.settimeout(5)
                    session.wait_for(lambda s: s.application.get("connected") == 1)
                    peer.sendall(b"\x1b[=6h")
                    exact(peer, records((6,)))
                    session.request(7, b"\x01\x00" + DESCRIPTOR)
                    caps = [(1,312,0,1), (1,304,0,1), (3,2,0,65534), (3,16,1,2),
                            (3,17,1,2), (1,544,0,1), (1,547,0,1), (1,545,0,1)]
                    exact(peer, records((1,1,2,1), *((2,1,*cap) for cap in caps), (4,1)))
                    report(session, 0, axis=0)
                    exact(peer, records(*((3,1,t,code,0) for t,code,_,_ in caps), (4,1)))
                    report(session, 0, button=2, axis=128, hat=3)
                    exact(peer, records((3,1,1,312,1), (3,1,3,2,32896), (3,1,3,16,2),
                                        (3,1,3,17,2), (3,1,1,547,1), (3,1,1,545,1), (4,1)))
                    report(session, 0, button=2, axis=255, hat=4)
                    exact(peer, records((3,1,3,2,65534), (3,1,3,16,0), (3,1,1,547,0), (4,1)))
                    report(session, 0, axis=0)
                    exact(peer, records((3,1,1,312,0), (3,1,3,2,0), (3,1,3,17,0), (3,1,1,545,0), (4,1)))

        # Earlier profiles contain only the ten descriptor fields.
        profile, = (root / "controllers").glob("*.map")
        profile.write_bytes(profile.read_bytes()[:10 * 4])
        with application(executable, root, reports=reports) as second:
            second.request(7, b"\x01\x00" + DESCRIPTOR)
            enter(second)
            shot = second.wait_text("Action pad south")
            assert any("Button 7" in row and "Action pad south" in row for row in shot.text.splitlines())

    with fixture(executable, settings="MainUI=Retro\n", reports=reports) as (client, _):
        client.wait_text("Directory:")
        client.key("f4")
        client.wait_text("Main Interface")
        client.request(7, b"\x01\x00" + DESCRIPTOR)
        client.request(7, b"\x01\x01" + DESCRIPTOR)
        report(client, 1)
        choose(client, "Map controls")
        client.wait_text("Choose a controller")
        client.key("f1")
        client.wait_text("Choose what a BBS receives")
        client.key("escape")
        choose(client, "#2  Fixture gamepad")
        client.wait_text("BBS inputs")
        choose(client, "Action pad south")
        client.wait_text("Choose control for Action pad south")
        report(client, 1, button=2)
        client.wait_for(lambda s: any("Button 2" in row and "Action pad south" in row for row in s.lines))


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="syncterm-controller-tests-") as diagnostics:
        if sys.argv[2] == "mapper":
            mapper(sys.argv[1], diagnostics)
        else:
            connected(sys.argv[1], sys.argv[2], diagnostics)
