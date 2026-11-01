import "syncterm" for Screen, Surface
import "wren_console" for WrenConsole

class WrenConsoleTest {
  static check_(ok, label) {
    if (ok) {
      __pass = __pass + 1
    } else {
      __fail = __fail + 1
      System.print("  FAIL " + label)
    }
  }

  static run() {
    __pass = 0
    __fail = 0

    check_(WrenConsole.quoteString_("abc\n") == "\"abc\\n\"",
        "quoting escapes newline")
    var special = String.fromByte(0x22) + String.fromByte(0x5c) +
        String.fromByte(0x0d) + String.fromByte(0x09) +
        String.fromByte(0) + String.fromByte(1) + String.fromByte(0x7f)
    check_(WrenConsole.quoteString_(special) ==
        "\"\\\"\\\\\\r\\t\\0\\x01\\x7f\"",
        "quoting preserves the established C-style escapes")
    check_(WrenConsole.quoteString_("é一") == "\"é一\"",
        "quoting preserves valid Unicode text")
    check_(WrenConsole.quoteString_(String.fromByte(0x80)) == "\"\\x80\"",
        "quoting escapes malformed UTF-8 bytes")
    var overlong = String.fromByte(0xc0) + String.fromByte(0xaf)
    check_(WrenConsole.quoteString_(overlong) == "\"\\xc0\\xaf\"",
        "quoting rejects non-canonical UTF-8")
    check_(WrenConsole.killWord_("abc def") == "abc ",
        "ASCII kill-word retains preceding word")
    check_(WrenConsole.killWord_("é一 x") == "é一 ",
        "kill-word retains preceding multibyte word")

    var received = []
    WrenConsole.register("edit", "test", Fn.new {|args| received.add(args) })
    WrenConsole.runCommand_("/edit é一", "main_menu")
    check_(received.count == 1 && received[0] == "é一",
        "registered command receives full Unicode arguments")
    check_(WrenConsole.runCommand_("/in módulo", "main_menu") == "módulo",
        "/in receives the full Unicode module name")

    var controls = String.fromByte(1) + String.fromCodePoint(0x85) +
        String.fromByte(0x80) + String.fromByte(0xed) +
        String.fromByte(0xa0) + String.fromByte(0x80)
    var pasted = WrenConsole.pasteText_(
        "α\r\n/edit β\n/edit γ\rδ\t一" + controls + "ε",
        "main_menu", "/edit ", "/edit ".bytes.count)
    check_(received.count == 4 && received[1] == "α" &&
        received[2] == "β" && received[3] == "γ",
        "paste normalizes CR, LF, and CRLF submissions")
    check_(pasted[0] == "main_menu" && pasted[1] == "δ\t一ε" &&
        pasted[2] == pasted[1].bytes.count,
        "paste keeps Unicode and Tab while dropping invalid controls")

    // The process harness sends actual text and editing events during this call.
    WrenConsole.run("main_menu")
    check_(received.count == 6 && received[4] == "AΩZ" &&
        received[5] == "keep done",
        "interactive editing preserves codepoint boundaries")
    WrenConsole.unregister("edit")

    WrenConsole.drawPrompt_(1, "m", "A\u0301B", "A\u0301".bytes.count)
    var position = Screen.window.position
    check_(position[0] == 12 && position[1] == 1,
        "prompt cursor follows the NFC grapheme width")
    WrenConsole.drawPrompt_(1, "m", "A\tB", 2)
    position = Screen.window.position
    check_(position[0] == 17 && position[1] == 1,
        "prompt cursor follows eight-column tab stops")

    return [__pass, __fail]
  }

  // The prompt width check requires the synthetic C fixture's wide mapping.
  static runSyntheticFontAudit() {
    __pass = 0
    __fail = 0
    WrenConsole.drawPrompt_(1, "m", "A一B", "A一".bytes.count)
    var position = Screen.window.position
    check_(position[0] == 14 && position[1] == 1,
        "prompt cursor accounts for a wide glyph")
    var saved = Screen.save()
    var width = Screen.size[0]
    Screen.window.bounds = [1, 1, width, Screen.size[1]]
    var original = Surface.new(width, 3)
    original.cellAt(0, 0).ch = "A"
    original.cellAt(0, 1).ch = "B"
    original.cellAt(0, 2).ch = "C"
    Screen.putRect(original, 1, 1)
    WrenConsole.scrollViewportUp1_(["X", "A", "B", "C"], 0, 3)
    var rows = Screen.readRect(1, 1, 1, 3)
    check_(rows[0].ch == "X" && rows[1].ch == "A" && rows[2].ch == "B",
        "scrolling up preserves existing viewport rows")
    WrenConsole.scrollViewportDown1_(["X", "A", "B", "C"], 1, 3)
    rows = Screen.readRect(1, 1, 1, 3)
    check_(rows[0].ch == "A" && rows[1].ch == "B" && rows[2].ch == "C",
        "scrolling down preserves existing viewport rows")
    WrenConsole.scrollViewportUp1_(["Z"], 0, 1)
    check_(Screen.readRect(1, 1, 1, 1)[0].ch == "Z",
        "one-row viewport repaints without an empty capture")
    Screen.restore(saved)
    return [__pass, __fail]
  }
}
