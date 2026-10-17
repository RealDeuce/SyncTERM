// Self-tests for ui_help.  FakeApp records modal pushes/pops without
// pumping the event loop.

import "ui_widget"   for Rect
import "ui_help"     for Help
import "ui_markdown" for Markdown, MdRole
import "ui_style"    for Style, Theme
import "syncterm"    for KeyEvent, MouseEvent, Key, Mouse

class FakeApp {
  construct new() {
    _stack = []
  }
  modalStack { _stack }
  effectiveTheme { null }
  markDirty() {}
  pushModal(w) {
    w.parent = this
    _stack.add(w)
    return w
  }
  popModal() {
    if (_stack.count == 0) return null
    var w = _stack.removeAt(-1)
    w.parent = null
    return w
  }
  modal(w) {
    pushModal(w)
    return w
  }
}

class UiHelpTest {
  static run() {
    __pass = 0
    __fail = 0
    System.print("=== ui_help self-test starting ===")

    testSplitLines_()
    testSplitLinesEmpty_()
    testSplitLinesNoTrailing_()
    testConstruction_()
    testMenuDefinitionListFormatting_()
    testProtocolLogFormatting_()
    testUnmatchedInlineMarkersRemainLiteral_()
    testPreformattedUsesHardLines_()
    testPreformattedPreservesInlineMarkup_()
    testEscDismisses_()
    testEnterDismisses_()
    testQuitFallsThrough_()
    testDownScrollsByOne_()
    testUpScrollsByOne_()
    testHomeJumpsToTop_()
    testEndJumpsToBottom_()
    testPageDownAdvancesByViewport_()
    testWheelDownScrolls_()
    testScrollbarWheelMovesPage_()
    testScrollbarDefaultsRight_()
    testRandomKeyDismisses_()
    testOrdinaryClickDismisses_()
    testMiddleClickDismisses_()
    testDragFallsThrough_()
    testSeparatorReflows_()
    testCodeSpanKeepsLiteralMarkup_()
    testCodeSpanRestoresSurroundingRole_()
    testBulletHardBreakKeepsIndent_()
    testBulletHardBreakChain_()
    testBulletWithoutHardBreakDoesNotContinue_()
    testThemeFontReflows_()

    var total = __pass + __fail
    System.print("=== ui_help: %(total) tests, %(__pass) pass, %(__fail) fail ===")
    return [__pass, __fail]
  }

  static check_(ok, label) {
    if (ok) {
      __pass = __pass + 1
    } else {
      __fail = __fail + 1
      System.print("  FAIL %(label)")
    }
  }

  static testCodeSpanKeepsLiteralMarkup_() {
    var runs = Markdown.parseInline_("`**literal**`", MdRole.text)
    check_(runs.count == 1 && runs[0].text == "**literal**" &&
           runs[0].role == MdRole.code,
           "Markdown: bold delimiters inside a code span remain literal")
  }

  static testCodeSpanRestoresSurroundingRole_() {
    var runs = Markdown.parseInline_(
        "**before `**literal\\**` after**", MdRole.text)
    check_(runs.count == 3 && runs[0].text == "before " &&
        runs[0].role == MdRole.bold &&
        runs[1].text == "**literal\\**" && runs[1].role == MdRole.code &&
        runs[2].text == " after" && runs[2].role == MdRole.bold,
        "Markdown: code is literal and closing it resumes surrounding bold")
  }

  static testBulletHardBreakKeepsIndent_() {
    var lines = Markdown.layout(Markdown.parse("- first  \nsecond"), 30)
    var second = ""
    if (lines.count > 1) {
      for (run in lines[1].runs) second = second + run.text
    }
    check_(lines.count == 2 && second == "  second",
           "Markdown: bullet hard-break continuation retains its hanging indent")
    lines = Markdown.layout(Markdown.parse("first  \nsecond"), 30)
    second = ""
    if (lines.count > 1) {
      for (run in lines[1].runs) second = second + run.text
    }
    check_(lines.count == 2 && second == "second",
           "Markdown: paragraph hard break retains the next line")
  }

  static lineText_(line) {
    var text = ""
    for (run in line.runs) text = text + run.text
    return text
  }

  static testBulletHardBreakChain_() {
    var lines = Markdown.layout(Markdown.parse(
        "- first  \n**second**  \nthird\noutside"), 30)
    var hasBold = false
    if (lines.count > 1) {
      for (run in lines[1].runs) {
        if (run.text == "second" && run.role == MdRole.bold) hasBold = true
      }
    }
    check_(lines.count == 4 && lineText_(lines[1]) == "  second" &&
        lineText_(lines[2]) == "  third" &&
        lineText_(lines[3]) == "outside" && hasBold,
        "Markdown: chained bullet hard breaks retain indent and inline roles")
  }

  static testBulletWithoutHardBreakDoesNotContinue_() {
    var lines = Markdown.layout(Markdown.parse("- first\nsecond"), 30)
    check_(lines.count == 2 && lineText_(lines[0]) == "• first" &&
        lineText_(lines[1]) == "second",
        "Markdown: an ordinary source line is not owned by a bullet")
  }

  static testThemeFontReflows_() {
    var wide = Theme.new({
      "default": Style.new(255, 0x07, 0xaaaaaa, 0x000000)
    }, Theme.default.glyphs)
    var narrow = Theme.new({
      "default": Style.new(0, 0x07, 0xaaaaaa, 0x000000)
    }, Theme.default.glyphs)
    var help = Help.new("Help", "AA AA")
    help.bounds = Rect.new(1, 1, 10, 5)
    help.theme = wide
    help.handle(KeyEvent.new(Key.end))
    var wideTop = help.scrollTop
    help.theme = narrow
    help.handle(KeyEvent.new(Key.end))
    check_(wideTop == 1 && help.scrollTop == 0,
        "Help: changing the effective font invalidates wrapped layout")
  }

  static testSeparatorReflows_() {
    var body = "aaaa bbbbb aaaa bbbbb aaaa bbbbb"
    var changed = Help.new("Help", body)
    changed.bounds = Rect.new(1, 1, 14, 6)
    changed.handle(KeyEvent.new(Key.end))
    changed.scrollbarSeparator = false
    changed.handle(KeyEvent.new(Key.end))
    var fresh = Help.new("Help", body)
    fresh.bounds = Rect.new(1, 1, 14, 6)
    fresh.scrollbarSeparator = false
    fresh.handle(KeyEvent.new(Key.end))
    check_(changed.scrollTop == fresh.scrollTop,
           "Help: separator change reflows cached lines like initial layout")

    changed.scrollbarSeparator = true
    changed.handle(KeyEvent.new(Key.end))
    fresh = Help.new("Help", body)
    fresh.bounds = Rect.new(1, 1, 14, 6)
    fresh.scrollbarSeparator = true
    fresh.handle(KeyEvent.new(Key.end))
    check_(changed.scrollTop == fresh.scrollTop,
           "Help: restoring the separator reflows and clamps cached layout")
  }

  // Build a help with `lineCount` rendered rows by emitting one bullet
  // per row — the markdown layout treats consecutive bullets as
  // separate blocks, so each maps to a single MdLine.
  static makeHelp_(lineCount, w, h) {
    var body = ""
    var i = 0
    while (i < lineCount) {
      if (i > 0) body = body + "\n"
      body = body + "- line %(i)"
      i = i + 1
    }
    var hp = Help.new("Help", body)
    hp.bounds = Rect.new(1, 1, w, h)
    return hp
  }

  // ----- Markdown.splitLines_ -------------------------------------

  static testSplitLines_() {
    var lines = Markdown.splitLines_("a\nb\nc")
    check_(lines.count == 3 && lines[0] == "a" && lines[1] == "b" &&
           lines[2] == "c",
           "Markdown.splitLines_: 'a\\nb\\nc' -> 3 lines")
  }

  static testSplitLinesEmpty_() {
    var lines = Markdown.splitLines_("")
    check_(lines.count == 1 && lines[0] == "",
           "Markdown.splitLines_: empty string -> [\"\"]")
  }

  static testSplitLinesNoTrailing_() {
    var lines = Markdown.splitLines_("only")
    check_(lines.count == 1 && lines[0] == "only",
           "Markdown.splitLines_: no newline -> single line")
  }

  // ----- Construction --------------------------------------------

  static testConstruction_() {
    var h = makeHelp_(3, 30, 8)
    check_(h.title == "Help" && h.scrollTop == 0 &&
           h.scrollbarSide == "right" && h.scrollbarSeparator == true,
           "Help: title set, scrollTop starts at 0, scrollbar on right")
  }

  static testMenuDefinitionListFormatting_() {
    var doc = Markdown.parse("# Settings\n\nName\n:  Entry name\nPort\n:  TCP port")
    var lines = Markdown.layout(doc, 60)
    check_(lines.count == 4 &&
           lines[0].runs[0].role == "help.heading.1" &&
           lines[2].runs[0].text == "Name" &&
           lines[2].runs[0].role == "help.bold" &&
           lines[3].runs[0].text == "Port",
           "Help: menu headings and definition lists retain formatting")
  }

  static testUnmatchedInlineMarkersRemainLiteral_() {
    var doc = Markdown.parse("Ctrl-` and **literal")
    var lines = Markdown.layout(doc, 60)
    var text = ""
    var ordinary = true
    for (run in lines[0].runs) {
      text = text + run.text
      if (run.role != "help.text") ordinary = false
    }
    check_(lines.count == 1 && text == "Ctrl-` and **literal" && ordinary,
           "Markdown: unmatched inline markers remain literal")
  }

  static testProtocolLogFormatting_() {
    var lines = Markdown.layout(Markdown.parse(
        "# Connection Details\n\nClosed  \nConnected: 00:00:12\n\n" +
        "# Protocol Session Log\n\n" +
        "- SSH Info: \\`key\\` \\*\\*value\\*\\* \\\\path\n" +
        "- # literal heading\n- : literal definition"), 80)
    check_(lines.count == 10 &&
        lineText_(lines[0]) == " Connection Details " &&
        lineText_(lines[2]) == "Closed" &&
        lineText_(lines[3]) == "Connected: 00:00:12" &&
        lineText_(lines[5]) == " Protocol Session Log " &&
        lineText_(lines[7]) == "• SSH Info: `key` **value** \\path" &&
        lineText_(lines[8]) == "• # literal heading" &&
        lineText_(lines[9]) == "• : literal definition",
        "Help: v1.10a protocol log format preserves lines and literal markup")
    var literal = true
    for (run in lines[7].runs) {
      if (run.role != MdRole.text && run.role != MdRole.bullet) literal = false
    }
    check_(literal, "Help: protocol log content is not styled as Markdown")
  }

  static testPreformattedUsesHardLines_() {
    var h = Help.new("Report", "  one\n\nSection\n  two\n  three")
    h.preformatted = true
    h.bounds = Rect.new(1, 1, 30, 8)
    h.handle(KeyEvent.new(Key.end))
    check_(h.preformatted && h.scrollTop == 1,
           "Help: preformatted body preserves hard lines")
  }

  static testPreformattedPreservesInlineMarkup_() {
    var h = Help.new("Report", "    [`√`] option")
    h.preformatted = true
    var runs = h.preformattedLines_()[0].runs
    check_(runs.count == 3 && runs[0].text == "    [" &&
           runs[1].text == "√" && runs[1].role == "help.code" &&
           runs[2].text == "] option",
           "Help: preformatted body keeps indentation and highlights")
  }

  // ----- Dismiss --------------------------------------------------

  static testEscDismisses_() {
    var app = FakeApp.new()
    var h   = makeHelp_(3, 30, 8)
    app.modal(h)
    h.handle(KeyEvent.new(Key.escape))
    check_(app.modalStack.count == 0,
           "Help: Esc dismisses")
  }

  static testEnterDismisses_() {
    var app = FakeApp.new()
    var h   = makeHelp_(3, 30, 8)
    app.modal(h)
    h.handle(KeyEvent.new(Key.enter))
    check_(app.modalStack.count == 0,
           "Help: Enter dismisses")
  }

  static testQuitFallsThrough_() {
    var app = FakeApp.new()
    var h = makeHelp_(3, 30, 8)
    app.modal(h)
    check_(!h.handle(KeyEvent.new(Key.quit)) &&
        app.modalStack.count == 1,
        "Help: process-close key falls through to the App")
  }

  // ----- Scroll ---------------------------------------------------

  static testDownScrollsByOne_() {
    // 20-line body, 5-row viewport (h=9 minus 4 for frame + padding).
    var h = makeHelp_(20, 30, 9)
    h.handle(KeyEvent.new(Key.down))
    check_(h.scrollTop == 1,
           "Help: Down scrolls scrollTop by 1")
  }

  static testUpScrollsByOne_() {
    var h = makeHelp_(20, 30, 9)
    h.handle(KeyEvent.new(Key.down))
    h.handle(KeyEvent.new(Key.down))
    h.handle(KeyEvent.new(Key.up))
    check_(h.scrollTop == 1,
           "Help: Up scrolls scrollTop by -1")
  }

  static testHomeJumpsToTop_() {
    var h = makeHelp_(20, 30, 9)
    h.handle(KeyEvent.new(Key.end))
    h.handle(KeyEvent.new(Key.home))
    check_(h.scrollTop == 0,
           "Help: Home -> scrollTop 0")
  }

  static testEndJumpsToBottom_() {
    var h = makeHelp_(20, 30, 9)             // viewport = 5
    h.handle(KeyEvent.new(Key.end))
    check_(h.scrollTop == 15,
           "Help: End -> scrollTop count - viewport")
  }

  static testPageDownAdvancesByViewport_() {
    var h = makeHelp_(20, 30, 9)             // viewport = 5
    h.handle(KeyEvent.new(Key.pageDown))
    check_(h.scrollTop == 5,
           "Help: PageDown advances by viewport rows")
  }

  static testWheelDownScrolls_() {
    var h = makeHelp_(20, 30, 9)
    var ev = MouseEvent.new(Mouse.wheelDown, 5, 3, 5, 3)
    var consumed = h.handle(ev)
    check_(consumed && h.scrollTop == 1,
           "Help: mouse wheel scrolls one line")
  }

  static testScrollbarWheelMovesPage_() {
    var h = makeHelp_(20, 30, 9)             // viewport = 5
    var ev = MouseEvent.new(Mouse.wheelDown, 29, 3, 29, 3)
    var consumed = h.handle(ev)
    check_(consumed && h.scrollTop == 5,
           "Help: mouse wheel over scrollbar scrolls one page")
  }

  static testScrollbarDefaultsRight_() {
    var h = makeHelp_(20, 30, 9)
    var sf = h.draw()
    var consumed = h.handle(
        MouseEvent.new(Mouse.button1Click, 29, 8, 29, 8))
    check_(sf.cellAt(28, 1).chByte == 0x1E &&
           sf.cellAt(27, 1).ch == "│" && consumed && h.scrollTop == 1,
           "Help: right scrollbar draws and accepts its down arrow")
  }

  static testRandomKeyDismisses_() {
    var app = FakeApp.new()
    var h = makeHelp_(3, 30, 8)
    app.modal(h)
    var consumed = h.handle(KeyEvent.new(0x41))   // 'A'
    check_(consumed && app.modalStack.count == 0,
           "Help: ordinary printable key dismisses")
  }

  static testOrdinaryClickDismisses_() {
    var app = FakeApp.new()
    var h = makeHelp_(3, 30, 8)
    app.modal(h)
    var ev = MouseEvent.new(Mouse.button1Click, 5, 3, 5, 3)
    var consumed = h.handle(ev)
    check_(consumed && app.modalStack.count == 0,
           "Help: ordinary button-1 click dismisses")
  }

  static testMiddleClickDismisses_() {
    var app = FakeApp.new()
    var h = makeHelp_(3, 30, 8)
    app.modal(h)
    var ev = MouseEvent.new(Mouse.button2Click, 5, 3, 5, 3)
    var consumed = h.handle(ev)
    check_(consumed && app.modalStack.count == 0,
           "Help: ordinary middle click dismisses")
  }

  static testDragFallsThrough_() {
    var h = makeHelp_(20, 30, 9)
    var ev = MouseEvent.new(Mouse.button1DragStart, 1, 3, 5, 3)
    var consumed = h.handle(ev)
    check_(!consumed, "Help: mouse drag falls through to screen selection")
  }
}
