// Expected masks and state are independent of the native binding helpers.
import "syncterm" for Cell, CellStyle, Surface, Screen

class CellStyleChecks {
  static check(ok) {
    if (!ok) Fiber.abort("Cell style contract")
  }
  static rejects(action) {
    var fiber = Fiber.new(action)
    fiber.try()
    check(fiber.error is String)
  }
  static allWithDouble { CellStyle.all & ~CellStyle.underline }
  static properties(c) {
    return [c.bright, c.blink, c.underline, c.bold, c.reverse, c.conceal, c.fastBlink, c.strikethrough, c.doubleUnderline]
  }
  static setters(c) {
    return [Fn.new {|v| c.bright = v }, Fn.new {|v| c.blink = v },
      Fn.new {|v| c.underline = v }, Fn.new {|v| c.bold = v },
      Fn.new {|v| c.reverse = v }, Fn.new {|v| c.conceal = v }, Fn.new {|v| c.fastBlink = v },
      Fn.new {|v| c.strikethrough = v }, Fn.new {|v| c.doubleUnderline = v }]
  }
  static run() {
    var bits = [CellStyle.bright, CellStyle.blink, CellStyle.underline,
      CellStyle.bold, CellStyle.reverse, CellStyle.conceal, CellStyle.fastBlink, CellStyle.strikethrough, CellStyle.doubleUnderline]
    check(bits[0] == 0x400000 && bits[1] == 2 && bits[2] == 0x10000)
    check(bits[3] == 0x20000 && bits[4] == 0x40000 && bits[5] == 4)
    check(bits[6] == 0x2000000 && bits[7] == 0x4000000 && bits[8] == 0x100000 && CellStyle.all == 0x6570006)
    var c = Cell.new()
    c.ch = "A"
    c.fgRgb = 0x112233
    c.bgRgb = 0x445566
    check(!c.teletext && !c.doubleHeight && !c.reveal && !c.separated)
    check(c.teletextControl == null && !c.pixelGraphics)
    for (combination in 0...512) {
      var mask = 0
      for (i in 0...9) {
        if ((combination & (1 << i)) != 0) mask = mask | bits[i]
      }
      c.styleFlags = mask
      var effective = (mask & CellStyle.doubleUnderline) != 0 ? mask & ~CellStyle.underline : mask
      check(c.styleFlags == effective)
      var values = properties(c)
      var individual = Cell.new()
      var write = setters(individual)
      for (i in 0...9) {
        var expected = (combination & (1 << i)) != 0
        check(values[i] == ((effective & bits[i]) != 0))
        write[i].call(expected)
      }
      check(individual.styleFlags == effective)
      check(c.ch == "A" && c.fgRgb == 0x112233 && c.bgRgb == 0x445566)
    }
    c.doubleUnderline = true
    c.underline = true
    check(c.underline && !c.doubleUnderline)
    c.doubleUnderline = true
    check(c.doubleUnderline && !c.underline)
    c.styleFlags = CellStyle.bold
    for (bad in [null, true, "1", [], {}, -1, 0.5, 0/0, 1/0, -1/0,
      1, 8, 0x10, 0x20, 0x40, 0x80, 0x100, 0x200, 0x80000,
      0x800000, 0x1000000, 0x80000000, 4294967296, 9007199254740992,
      CellStyle.all | 8]) {
      rejects(Fn.new { c.styleFlags = bad })
      check(c.styleFlags == CellStyle.bold)
    }
    for (write in setters(c)) {
      for (bad in [null, 0, 1, "true", [], {}, 0/0, 1/0]) {
        rejects(Fn.new { write.call(bad) })
        check(c.styleFlags == CellStyle.bold)
      }
    }
    var s = Surface.new(2, 1)
    var alias = s[0]
    alias.ch = "A"
    alias.styleFlags = CellStyle.all
    check(s[0].styleFlags == allWithDouble)
    check(s.cellAt(1, 0, alias))
    alias.ch = "B"
    alias.styleFlags = CellStyle.underline
    check(s[0].styleFlags == allWithDouble && s[1].underline)
    check(!s.cellAt(2, 0, alias) && alias.underline)
    var copy = Surface.new(2, 1)
    copy.putRect(s, 0, 0)
    check(copy[0].eqContent(s[0]) && copy[1].eqContent(s[1]))
    alias = copy[0]
    copy = null
    System.gc()
    check(alias.styleFlags == allWithDouble)
    alias.styleFlags = 0
    check(!alias.bold && s[0].styleFlags == allWithDouble)
    var wide = Surface.new(2, 1)
    wide[0].font = 0
    wide[0].ch = "一"
    check(wide[0].columns == 2 && wide[1].columns == 0)
    wide[0].styleFlags = CellStyle.bold
    wide[1].styleFlags = CellStyle.underline
    check(wide[0].columns == 2 && wide[1].columns == 0)
    check(wide[0].ch == "一" && wide[1].ch == "一")
    Screen.putRect(wide, 10, 1)
    var roundtrip = Screen.readRect(10, 1, 11, 1)
    check(roundtrip[0].columns == 2 && roundtrip[1].columns == 0)
    check(roundtrip[0].bold && roundtrip[1].underline)
  }
  static metadataBits(teletext, doubleHeight, reveal, separated, control) {
    var c = Screen.readRect(1, 1, 1, 1)[0]
    check(c.teletext == teletext && c.doubleHeight == doubleHeight)
    check(c.reveal == reveal && c.separated == separated && c.teletextControl == control)
  }
  static metadata() {
    var s = Screen.readRect(1, 1, 1, 1)
    var c = s[0]
    check(c.teletext && c.doubleHeight && c.reveal && c.separated)
    check(c.teletextControl == 0x5d && c.styleFlags == allWithDouble)
    var text = c.ch
    var font = c.font
    var fg = c.fgRgb
    var bg = c.bgRgb
    for (write in [Fn.new { c.teletext = false }, Fn.new { c.doubleHeight = false },
      Fn.new { c.reveal = false }, Fn.new { c.separated = false },
      Fn.new { c.teletextControl = 0 }, Fn.new { c.pixelGraphics = false }]) rejects(write)
    rejects(Fn.new { c.styleFlags = CellStyle.bold | 0x10 })
    check(c.styleFlags == allWithDouble)
    c.styleFlags = CellStyle.bold | CellStyle.underline
    check(c.teletext && c.doubleHeight && c.reveal && c.separated)
    check(c.teletextControl == 0x5d && c.ch == text && c.font == font)
    check(c.fgRgb == fg && c.bgRgb == bg)
    Screen.putRect(s, 3, 1)
    check(Screen.readRect(3, 1, 3, 1)[0].teletextControl == 0x5d)
    var plain = Screen.readRect(2, 1, 2, 1)[0]
    check(!plain.teletext && plain.teletextControl == null)
  }
  static graphics() {
    var captured = Screen.readRect(1, 3, 2, 3)
    for (c in captured) {
      check(c.pixelGraphics)
      c.styleFlags = CellStyle.all
      c.bold = false
      check(c.pixelGraphics && !c.bold)
    }
    var copy = Surface.new(2, 1)
    copy.putRect(captured, 0, 0)
    captured = null
    System.gc()
    Screen.putRect(copy, 1, 4)
  }
  static bitmap() {
    var s = Screen.readRect(1, 6, 4, 6)
    var masks = [CellStyle.bold, CellStyle.underline, CellStyle.reverse, CellStyle.conceal, CellStyle.fastBlink, CellStyle.strikethrough]
    for (i in 0...4) {
      s[i].fgRgb = 0x112233
      s[i].bgRgb = 0x445566
      s[i].styleFlags = masks[i]
    }
    Screen.putRect(s, 1, 6)
  }
}
CellStyleChecks.run()
