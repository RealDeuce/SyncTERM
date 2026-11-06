// Independent conversion expectations; native table parity is checked separately.
import "syncterm" for Codepage, Font, Screen

class CodepageChecks {
  static check(ok) {
    if (!ok) Fiber.abort("codepage query contract")
  }
  static rejects(action) {
    var fiber = Fiber.new(action)
    fiber.try()
    check(fiber.error is String)
  }
  static vectors() {
    check(Codepage.encodeByte(Codepage.cp1251, 0x0410) == 0xc0)
    check(Codepage.encodeByte(Codepage.cp437, 0x0410) == null)
    check(Codepage.encodeByte(Codepage.cp437, 0x1f642) == null)
    check(Codepage.encodeByte(Codepage.cp437, 0x10ffff) == null)
    check(Codepage.encodeByte(Codepage.cp437, 0xd7ff) == null)
    check(Codepage.encodeByte(Codepage.cp437, 0xe000) == null)
    check(Codepage.encodeByte(Codepage.cp437, 0) == 0)
    check(Codepage.encodeByte(Codepage.cp437, 0x7c) == 0x7c)
    check(Codepage.encodeByte(Codepage.cp437, 0xa6) == 0x7c)
    check(Codepage.encodeByte(Codepage.atascii, 0x21b0) == 0x7d)
    check(Codepage.encodeByte(Codepage.atascii, 0x1f8b0) == 0x7d)
    check(Codepage.encodeByte(Codepage.cp866m, 0x2248) == 0xf7)
    // A scalar is used as given, without text normalization or replacement.
    check(Codepage.encodeByte(Codepage.iso8859_1, 0xe9) == 0xe9)
    check(Codepage.encodeByte(Codepage.iso8859_1, 0x301) == null)
    for (pair in [[0, "\u0000"], [1, "☺"], [0x7c, "¦"], [0x7f, "⌂"], [0xb3, "│"], [0xff, "\u00a0"]]) {
      check(Codepage.decodeByte(Codepage.cp437, pair[0], true) == pair[1])
      check(Codepage.decodeByte(Codepage.cp437, pair[0]) == pair[1])
    }
    check(Codepage.decodeByte(Codepage.cp437, 0, false) == "\u0000")
    check(Codepage.decodeByte(Codepage.cp437, 1, false) == "\u0001")
    check(Codepage.decodeByte(Codepage.cp437, 0x7c, false) == "|")
    check(Codepage.decodeByte(Codepage.cp437, 0x7f, false) == "\u007f")
    check(Codepage.decodeByte(Codepage.cp1251, 0xc0, false) == "А")
    check(Codepage.decodeByte(Codepage.cp1251, 0xc0, true) == "А")
    check(Codepage.brokenVertical(Codepage.cp437))
    check(!Codepage.brokenVertical(Codepage.cp1251))
    check(Codepage.brokenVertical(Codepage.cp1251_b))
    check(Font.builtinBrokenVertical(0))
    check(!Font.builtinBrokenVertical(1))
    check(!Font.builtinBrokenVertical(Font.ripterm))
  }
  static invalid() {
    for (cp in [-1, 29, 0.5, null, true, "0", [], {}, 0/0, 1/0, -1/0, 9007199254740992]) {
      rejects(Fn.new { Codepage.encodeByte(cp, 65) })
      rejects(Fn.new { Codepage.brokenVertical(cp) })
      rejects(Fn.new { Codepage.decodeByte(cp, 65, true) })
      rejects(Fn.new { Codepage.decodeByte(cp, 65) })
    }
    for (scalar in [-1, 0x110000, 0xd800, 0xdfff, 0.5, null, true, "A", "\xff", [], {}, 0/0, 1/0, -1/0, 9007199254740992]) {
      rejects(Fn.new { Codepage.encodeByte(Codepage.cp437, scalar) })
    }
    for (byte in [-1, 256, 0.5, null, true, "A", [], {}, 0/0, 1/0]) {
      rejects(Fn.new { Codepage.decodeByte(Codepage.cp437, byte, false) })
      rejects(Fn.new { Codepage.decodeByte(Codepage.cp437, byte) })
    }
    for (graphic in [null, 0, 1, "true", [], {}]) {
      rejects(Fn.new { Codepage.decodeByte(Codepage.cp437, 65, graphic) })
    }
    for (selector in [-1, 46, 255, 256, 0.5, null, false, "0", [], {}, 0/0, 1/0]) {
      rejects(Fn.new { Font.builtinBrokenVertical(selector) })
    }
    vectors()
  }
  static native(cp, byte, graphic, scalar, encoded) {
    check(Codepage.decodeByte(cp, byte, graphic) == String.fromCodePoint(scalar))
    if (graphic) check(Codepage.decodeByte(cp, byte) == String.fromCodePoint(scalar))
    check(Codepage.encodeByte(cp, scalar) == encoded)
  }
  static fonts() {
    var old = Screen.font[0]
    Screen.font[0] = 1
    check(Codepage.encodes_("А") && !Codepage.encodes_("└"))
    vectors()
    Screen.font[0] = 249
    check(!Font.available(249) && !Codepage.encodes_("A"))
    vectors()
    Screen.font[0] = old
  }
}
CodepageChecks.vectors()
CodepageChecks.invalid()
CodepageChecks.fonts()
