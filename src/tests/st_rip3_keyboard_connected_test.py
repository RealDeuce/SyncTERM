#!/usr/bin/env python3
"""RIPv3 hotkeys and Tab navigation through TCP and the real event path."""
# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
import argparse
from pathlib import Path

from st_migrated_control_test import connection
from st_rip3_buttons_connected_test import button,style,remove_fields
from st_rip3_connected_test import pixel, query
from st_rip3_mouse_connected_test import field
from st_terminal_conformance_test import Terminal
from syncterm_control import CONTROL, SHIFT
from syncterm_fixture import receive


def check_autosense(t):
    ident = b"RIPSCRIP030001"
    t.case("rip3-autosense-selectors-and-first-parameter")
    t.send(b"\x1b[6553")
    t.send(b"9!")  # Low 16 bits are three. The peer waits for the response here.
    receive(t.peer, ident + b"\r", t.root / "replies.bin")
    t.exchange(b"\x1b[!\x1b[0;1!\x1b[65536!\x1b[3;2!\x1b[4!\x1b[?0!",
               ident * 4 + b"\r")

    t.case("rip3-autosense-disable-and-enable-within-one-input-buffer")
    t.exchange(b"\x1b[8;1H\x01|fHSDC|m0A0A\r\n")
    frame = t.exchange(b"\x1b[1;2!\x1b[!\x1b[3!"
                       b"\x1b[8;1H!|m0B0B\r\n" + query(b"ignored", at_cursor=True)
                       + b"\x1b[2!\x1b[3!" + query(b"$TEXTXY$", at_cursor=True),
                       ident + b"\r10:10")
    assert frame.lines[7].startswith("!|m0B0B")
    t.exchange(b"\x1b[1!\x1b[2!\x01|m0C0C\r\n" + query(b"$TEXTXY$", at_cursor=True), b"12:12")

    t.case("rip3-autosense-obeys-ansi-and-rip-input-boundaries")
    t.exchange(b"\x1b\x1b[!\x1b[12\x1b[!\x1b[12\x18\x1b[!"
               b"\x1b[12\x1a\x1b[!", ident + ident)
    t.exchange(b"\x1b[0" + query(b"inside", at_cursor=True) + b"!", b"inside" + ident)
    t.exchange(b"\x1b[1;1H!\x1b[!", ident)
    t.exchange(b"\x01|!embedded\x1b[3!\r\n" + query(b"alive", at_cursor=True), b"alive")
    t.exchange(query(b"$DTW$", at_cursor=True) + b"\x1b[!"
               + query(b"$ATW$alive", at_cursor=True), b"alive")
    t.exchange(b"\x1b[2J\x1b[H")


def check_tab_navigation(t):
    client = t.client

    def values(source, expected=b""):
        return t.exchange(query(source, at_cursor=True), expected)

    def key(code, expected=b"", modifiers=0):
        before = client.capture().metadata["events_dequeued"]
        client.key(code, modifiers)
        if expected:
            return t.input(expected)
        client.wait_for(lambda frame: frame.metadata["events_dequeued"] > before)
        return t.exchange()

    def same_pixels(actual, expected):
        assert actual.primary == expected.primary and actual.alternate == expected.alternate

    t.case("rip3-tab-traversal-and-activation")
    values(b"$MKILL$$OPTION(TAB,QUERY)$", b"1")
    first, second, third = (80, 90, 120, 120), (180, 90, 220, 120), (280, 90, 320, 120)
    before = t.exchange(field(first, b"ID=31:first:$FIELDID$")
                        + style(flags=256 | 1024 | 512)
                        + button(second, b"ID=32:<><>second:$FIELDID$", hotkey=13)
                        + field(third, b"ID=33:third:$FIELDID$"))
    highlighted = key("tab")
    # The half-open first rectangle is inflated by four pixels. XOR only
    # the perimeter, leaving both planes and all colors outside it intact.
    outline = {y * 640 + x for y in range(86, 124) for x in range(76, 124)
               if x in (76, 123) or y in (86, 123)}
    for old, new in ((before.primary, highlighted.primary),
                     (before.alternate, highlighted.alternate)):
        assert {i for i, (a, b) in enumerate(zip(old, new)) if a != b} == outline
    same_pixels(key("enter", b"first:31"), before)  # Beats the explicit CR binding.
    key("tab")
    key("tab")
    key("enter", b"second:32")
    key("tab", modifiers=SHIFT)  # No current field starts at the first slot.
    key("tab")
    key("tab", modifiers=SHIFT)  # Slot 1 steps back to the first field.
    key("enter", b"first:31")
    key("tab", modifiers=SHIFT)
    key("tab", modifiers=SHIFT)
    key("enter", b"third:33")  # Both directions wrap at their last slot.
    key("tab")
    key("tab")
    key("tab")
    key("tab")
    key("enter", b"first:31")

    t.case("rip3-tab-clearing-and-option-gates")
    key("tab")
    same_pixels(key("escape"), before)
    key("enter", b"second:32")
    key("tab")
    # Draw to the same native pixel while focused. Deselecting must reveal
    # the new indexed drawing, including in the alternate blink plane.
    focused = t.exchange(b"\x01|c0C|X262E\r\n")  # x=78, y=86
    assert pixel(focused, 78, 86) == 0x00aaaa
    cleared = values(b"$TABOFF$$OPTION(TAB)$", b"0")
    assert pixel(cleared, 78, 86) == 0xff5555
    assert cleared.primary[86 * 640 + 78] == cleared.alternate[86 * 640 + 78]
    key("tab", b"\t")
    values(b"$TABON$$OPTION(TAB)$", b"1")
    key("tab")
    still_focused = values(b"$TABOFF(1)$$OPTION(TAB)$", b"1")
    same_pixels(still_focused, focused)
    cleared_by_option = values(b"$OPTION(tab, off)$$OPTION(TAB,QUERY)$", b"0")
    same_pixels(cleared_by_option, cleared)
    values(b"$OPTION(TAB,ON)$$HKEYOFF$$OPTION(TAB)$", b"1")
    key("tab")
    key("enter", b"first:31")
    key("tab")
    same_pixels(values(b"$MKILL$$HKEYON$"), cleared)
    key("tab", b"\t")

    t.case("rip3-tab-focus-lifetime")
    before = t.exchange(field(first, b"ID=31:first:$FIELDID$")
                        + button(second, b"ID=32:<><>second:$FIELDID$", hotkey=13)
                        + field(third, b"ID=33:third:$FIELDID$", invert=True))
    key("tab")
    focused = key("tab")
    assert focused.primary != before.primary
    same_pixels(values(b"$TABON$$OPTION(LIST)$", b"DOORWAY,HOTKEY,TAB"), focused)
    same_pixels(values(b"$SMF$$RMF(3)$"), focused)
    same_pixels(t.exchange(b"\x01|1Mbad\r\n" + remove_fields((0, 250, 20, 270), 1)), focused)
    same_pixels(key(ord("q"), b"q"), focused)
    # Clicking another field commits its own action while keyboard focus
    # stays on B. The bitmap backend reports destination-scaled coordinates.
    m = focused.metadata
    px = m["destination_x"] + 600 * m["destination_width"] // (2 * 640)
    py = m["destination_y"] + 210 * m["destination_height"] // (2 * 480)
    client.mouse(px, py, action=1, buttons=1)
    client.mouse(px, py, action=1, buttons=0)
    same_pixels(t.input(b"third:33"), focused)
    client.mouse(px, py, action=1, buttons=1)
    held = client.wait_for(lambda frame: pixel(frame, 300, 105) == 0xffffff)
    for y in range(480):
        left_of_pressed = slice(y * 640, y * 640 + 270)
        assert held.primary[left_of_pressed] == focused.primary[left_of_pressed]
        assert held.alternate[left_of_pressed] == focused.alternate[left_of_pressed]
    same_pixels(key("enter", b"second:32"), before)
    dequeued = client.capture().metadata["events_dequeued"]
    client.mouse(px, py, action=1, buttons=0)
    client.wait_for(lambda frame: frame.metadata["events_dequeued"] > dequeued)
    same_pixels(t.exchange(), before)
    key("tab")
    key("tab")
    same_pixels(t.exchange(remove_fields(first, 1)), before)
    same_pixels(key("tab"), focused)  # With A removed, B is the first survivor.
    same_pixels(values(b"$RMF$"), before)
    key("tab")
    key("enter", b"first:31")
    key("tab")
    key("tab")
    dequeued = client.capture().metadata["events_dequeued"]
    client.focus(False)
    client.wait_for(lambda frame: frame.metadata["events_dequeued"] > dequeued)
    same_pixels(t.exchange(), before)
    client.focus(True)
    key("tab")
    key("enter", b"first:31")
    values(b"$MKILL$")

    t.case("rip3-tab-outline-clips-the-expanded-edges")
    before = t.exchange(field((0, 10, 12, 20), b"edge"))
    focused = key("tab")
    assert pixel(focused, 0, 6) != pixel(before, 0, 6)
    assert pixel(focused, 15, 10) != pixel(before, 15, 10)
    assert pixel(focused, 0, 10) == pixel(before, 0, 10)
    same_pixels(key("escape"), before)
    values(b"$MKILL$")


def run(args):
    with connection(args.executable,args.reports,encoding=args.encoding,
                    options="ScreenMode=Custom\nRIP=RIPv3\n",
                    settings="CustomColumns=80\nCustomRows=60\nCustomFontHeight=8\n"
                             "CustomAspectWidth=4\nCustomAspectHeight=3\n") as (client,root,peer,send):
        t=Terminal(client,root,peer,send)
        def values(source,expected=b""):
            return t.exchange(query(source,at_cursor=True),expected)
        def text(source,expected=b""):
            client.text(source)
            return t.input(expected) if expected else t.exchange()
        def key(code,expected=b"",modifiers=0):
            client.key(code,modifiers)
            return t.input(expected) if expected else t.exchange()
        t.exchange(b"\x1b[!",b"RIPSCRIP030001")
        check_autosense(t)
        t.exchange(b"\x01|*\r\n\x1b[?25l")
        t.case("rip3-keyboard-capability-and-engine-identity")
        values(b"A$RIPVER$B$RIPVER()$C$RIPVER(1)$D", b"ARIPSCRIP030001BRIPSCRIP030001CD")
        values(b"$IFS(DOORWAY)$:$IFS(ENG)$:$IFS(VT102)$:$IFS(EXTAPPS)$", b"1:1:0:0")
        values(b"$IFS(LIST, _EMULATIONS)$:$IFS(LIST, _LANGUAGES)$", b"ANSI,DOORWAY,RIPSCRIP:ENG")

        t.case("rip3-doorway-option-and-ansi-use-the-same-keyboard-mode")
        values(b"$OPTION(LIST)$:$OPTION(DOORWAY)$", b"DOORWAY,HOTKEY,TAB:0")
        key("up", b"\x1b[A")
        values(b"$OPTION(doorway, on)$$OPTION(DOORWAY, QUERY)$", b"1")
        key("up", b"\x00\x48")
        values(b"$DWAYOFF$$OPTION(DOORWAY)$", b"0")
        key("up", b"\x1b[A")
        values(b"$DWAYON$$OPTION(LIST, QUERY)$:$OPTION(DOORWAY)$", b"$OPTION(LIST, QUERY)$:1")
        t.exchange(b"\x01|2T10\r\n" + query(b"$CUR(TW)$", at_cursor=True), b"1")
        key("up", b"\x00\x48")
        t.exchange(b"\x1b[=255l")
        values(b"$DWAYON(1)$$OPTION(DOORWAY)$", b"0")
        key("up", b"\x1b[A")
        t.exchange(b"\x01|2T00\r\n\x1b[=255h")
        values(b"$OPTION(DOORWAY)$", b"1")
        values(b"$OPTION(DOORWAY,OFF)$$OPTION(DOORWAY)$", b"0")
        key("up", b"\x1b[A")

        frame=values(b"$WORLD(CUR,640,480)$")
        m=frame.metadata
        def mouse(x,y,left=False,move=False):
            px=m["destination_x"]+(2*x+1)*m["destination_width"]//(2*m["plane_width"])
            py=m["destination_y"]+(2*y+1)*m["destination_height"]//(2*m["plane_height"])
            before=client.capture().metadata["events_dequeued"]
            client.mouse(px,py,action=0 if move else 1,buttons=int(left))
            return client.wait_for(lambda s:s.metadata["events_dequeued"]>before)

        t.case("rip3-keyboard-literal-pr121-vectors")
        # These are the published bytes, independently of the local helper.
        t.exchange(b"!|1B00000200ZK000F000F080700000F07000000\r\n"
                   b"!|1U0A0A1E0U1T00ID=1:<>A<>A$FIELDID$\r\n"
                   b"!|1U0A141E1O2P00ID=2:<>a<>a$FIELDID$\r\n"
                   b"!|1U0A1Y1E2I0000ID=3:<>Zero<>Z$FIELDID$\r\n"
                   b"!|1U0A2S1E3C0D00ID=4:<>CR<>E$FIELDID$\r\n"
                   b"!|1U0A3M1E460020ID=5:<>Default 1<>D$FIELDID$\r\n"
                   b"!|1U0A4G1E500020ID=6:<>Default 2<>D$FIELDID$\r\n"
                   b"!|1U0A5A1E5U7300ID=7:<>Any<>W$FIELDID$\r\n")
        text("A",b"a2");text("a",b"a2");key("enter",b"E4")
        t.exchange(style(flags=256))
        text("a",b"a2")  # Bindings belong to instances, not the current style.
        t.exchange(style(flags=256|1024))
        mouse(20,20,True);text("a",b"a2");mouse(20,20)
        t.exchange()  # The published held-pointer/A vector has no later click.
        key("up",b"\x1b[A")  # Never match the encoded ESC against wildcard.
        text("\0",b"W7")
        text("\u00a0",b"W7")  # CP437 byte 255 remains an eligible wildcard byte.
        text("Ç",b"W7")  # CP437 byte 128 in either connection encoding.
        t.exchange(button((100,10,150,30),b"ID=8:<><>H$FIELDID$",hotkey=128))
        text("Ç",b"H8");text("ü",b"W7");text("ç",b"W7")
        # Delivered translated repeats activate once; raw notifications and
        # key-up do not supply duplicate candidates.
        client.physical_key(30,True)
        text("a",b"a2")
        client.physical_key(30,True,repeat=True)
        text("a",b"a2")
        client.physical_key(30,False)
        t.exchange()
        t.exchange(remove_fields((10,100,50,120),1));key("enter",b"D6")
        t.exchange(remove_fields((10,160,50,180),1));key("enter",b"D5")
        t.exchange(remove_fields((10,130,50,150),1));key("enter",b"W7")

        t.case("rip3-keyboard-consumption-and-gates")
        t.exchange(button((100,40,150,60),b"ID=9:<><>",hotkey=ord("Z"))
                   +button((100,70,150,90),b'<><>$RAND("bad")$',hotkey=ord("F")))
        text("z");text("f");text("f")  # No original key or wildcard fallback.
        mouse(300,250,move=True)
        values(b"$XY(DEVICE)$",b"0300:0250")
        text("a",b"a2")
        values(b"$XY(DEVICE)$",b"0300:0250")
        values(b"$OPTION(HOTKEY)$$HKEYOFF$$OPTION(HOTKEY,QUERY)$", b"10")
        mouse(20,20,True)
        text("a",b"a")  # Disabled lookup leaves original byte/case intact.
        mouse(20,20)
        t.input(b"A1")  # Disabled lookup did not cancel the pointer capture.
        values(b"$OPTION(HOTKEY,ON)$$OPTION(HOTKEY)$", b"1")
        text("a",b"a2")
        values(b"$OPTION(HOTKEY,OFF)$$OPTION(HOTKEY)$", b"0")
        text("a",b"a")
        values(b"$HKEYON$$OPTION(HOTKEY)$", b"1")
        text("a",b"a2")
        t.exchange(remove_fields((10,190,50,210),1))
        text("\0",b"\0")  # Hotkey zero remains unbound without the wildcard.
        t.exchange(button((100,100,150,120),b"ID=10:<><>control",hotkey=1))
        key(ord("a"),b"control",CONTROL)

        t.case("rip3-keyboard-pointer-cancellation-and-deletion")
        values(b"$MKILL$")
        before=t.exchange(style(flags=256|1024|2)
                          +button((10,10,60,40),b"ID=20:<><>hold",hotkey=ord("A"))
                          +button((100,10,150,40),b"ID=21:<><>$MKILL$gone:$FIELDID$",hotkey=ord("K")))
        mouse(20,20,True)
        frame=text("k",b"gone:21")
        assert frame.primary==before.primary and frame.alternate==before.alternate
        values(b"$XY(DEVICE)$",b"0020:0020")
        t.exchange(button((10,10,60,40),b"ID=20:<><>replacement",hotkey=ord("A")))
        mouse(20,20,True,move=True);mouse(20,20)
        t.exchange()
        text("a",b"replacement")
        t.exchange(button((200,10,250,40),b"ID=23:<><>$D(1)$$MKILL$delayed:$FIELDID$",hotkey=ord("Q")))
        text("q",b"delayed:23")
        text("a",b"a")

        t.case("rip3-keyboard-reset-before-host")
        t.exchange(style(flags=256|1024|4)
                   +button((10,100,60,130),b"ID=22:<><>reset:$FIELDID$:$CUR(BUT)$",hotkey=ord("R")))
        text("r",b"reset:22:0")
        text("a",b"a")
        values(b"$WORLD(CUR,640,480)$")
        check_tab_navigation(t)
        frame=values(b"alive",b"alive")
        assert frame.application["connected"]==1


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable",type=Path)
    parser.add_argument("encoding",choices=("CP437","UTF-8"))
    parser.add_argument("--reports",type=Path,required=True)
    run(parser.parse_args())
